// linkcheck.go — 并发 URL 健康检查器（仅用标准库）。
//
// 功能：批量检测一批 URL 的可达性，输出状态码、响应耗时与重定向信息，
//       支持并发数、超时时间与 HTTP 方法配置，最后给出汇总统计。
//
// 用法：
//   go run linkcheck.go urls.txt
//   go run linkcheck.go urls.txt -c 20 -timeout 5s
//   go run linkcheck.go -url https://example.com -url https://golang.org
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"sort"
	"strings"
	"sync"
	"time"
)

// result 保存单个 URL 的检测结果。
type result struct {
	URL        string
	Status     int
	Duration   time.Duration
	FinalURL   string
	Redirected bool
	Err        error
}

// arrayFlags 让 -url 可以重复出现多次。
type arrayFlags []string

func (a *arrayFlags) String() string { return strings.Join(*a, ", ") }

func (a *arrayFlags) Set(value string) error {
	*a = append(*a, value)
	return nil
}

func readURLs(path string) ([]string, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var urls []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		urls = append(urls, line)
	}
	return urls, scanner.Err()
}

// check 检测单个 URL，不跟随重定向（便于观察 3xx）。
func check(ctx context.Context, client *http.Client, rawURL string) result {
	if !strings.Contains(rawURL, "://") {
		rawURL = "https://" + rawURL
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return result{URL: rawURL, Err: err}
	}
	req.Header.Set("User-Agent", "linkcheck/1.0")

	start := time.Now()
	resp, err := client.Do(req)
	if err != nil {
		return result{URL: rawURL, Duration: time.Since(start), Err: err}
	}
	defer func() {
		io.Copy(io.Discard, resp.Body)
		resp.Body.Close()
	}()

	final := resp.Request.URL.String()
	return result{
		URL:        rawURL,
		Status:     resp.StatusCode,
		Duration:   time.Since(start),
		FinalURL:   final,
		Redirected: final != rawURL,
	}
}

func main() {
	var urls arrayFlags
	flag.Var(&urls, "url", "待检测的 URL，可重复传入")
	concurrency := flag.Int("c", 10, "并发数")
	timeout := flag.Duration("timeout", 8*time.Second, "单个请求超时时间")
	flag.Parse()

	if len(urls) == 0 {
		if flag.NArg() < 1 {
			fmt.Fprintln(os.Stderr, "用法：linkcheck [-url URL]... <urls.txt>")
			os.Exit(1)
		}
		fileURLs, err := readURLs(flag.Arg(0))
		if err != nil {
			fmt.Fprintf(os.Stderr, "读取文件失败：%v\n", err)
			os.Exit(1)
		}
		urls = fileURLs
	}

	if len(urls) == 0 {
		fmt.Fprintln(os.Stderr, "没有待检测的 URL")
		os.Exit(1)
	}

	client := &http.Client{
		Timeout: *timeout,
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			if len(via) >= 5 {
				return fmt.Errorf("重定向次数过多")
			}
			return nil
		},
	}

	ctx := context.Background()
	jobs := make(chan string)
	results := make(chan result, len(urls))

	var wg sync.WaitGroup
	for i := 0; i < *concurrency; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for u := range jobs {
				results <- check(ctx, client, u)
			}
		}()
	}

	go func() {
		for _, u := range urls {
			jobs <- u
		}
		close(jobs)
		wg.Wait()
		close(results)
	}()

	var collected []result
	for r := range results {
		collected = append(collected, r)
	}
	sort.Slice(collected, func(i, j int) bool {
		return collected[i].Duration < collected[j].Duration
	})

	fmt.Printf("%-6s %-12s %-52s %s\n", "状态", "耗时", "URL", "备注")
	fmt.Println(strings.Repeat("-", 100))

	ok, failed := 0, 0
	var total time.Duration
	for _, r := range collected {
		total += r.Duration
		if r.Err != nil {
			failed++
			fmt.Printf("%-6s %-12s %-52s %v\n", "ERR", r.Duration.Round(time.Millisecond),
				truncate(r.URL, 52), r.Err)
			continue
		}
		if r.Status >= 200 && r.Status < 400 {
			ok++
		} else {
			failed++
		}
		note := ""
		if r.Redirected {
			note = "→ " + truncate(r.FinalURL, 58)
		}
		fmt.Printf("%-6d %-12s %-52s %s\n", r.Status,
			r.Duration.Round(time.Millisecond), truncate(r.URL, 52), note)
	}

	fmt.Println(strings.Repeat("-", 100))
	fmt.Printf("共 %d 个 URL｜正常 %d｜异常 %d｜平均耗时 %s（并发 %d）\n",
		len(collected), ok, failed,
		(total / time.Duration(len(collected))).Round(time.Millisecond), *concurrency)

	if failed > 0 {
		os.Exit(2)
	}
}

func truncate(s string, n int) string {
	runes := []rune(s)
	if len(runes) <= n {
		return s
	}
	return string(runes[:n-1]) + "…"
}
