import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDate;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Scanner;

/**
 * TaskManager — 命令行待办事项管理器（仅用 JDK 标准库）。
 *
 * 功能：添加、列出、完成、删除、搜索、统计待办事项，
 *       并以 CSV 格式持久化到 ~/.todo-java/tasks.csv，下次启动自动加载。
 *
 * 用法：
 *   javac TaskManager.java && java TaskManager
 * 交互命令：
 *   add / list / done <id> / del <id> / find <关键字> / stats / quit
 */
public class TaskManager {

    /** 单个待办事项。 */
    static class Task {
        int id;
        String title;
        Priority priority;
        LocalDate dueDate;
        boolean done;

        Task(int id, String title, Priority priority, LocalDate dueDate, boolean done) {
            this.id = id;
            this.title = title;
            this.priority = priority;
            this.dueDate = dueDate;
            this.done = done;
        }

        String toCsv() {
            return String.join(",",
                    String.valueOf(id),
                    escape(title),
                    priority.name(),
                    dueDate == null ? "" : dueDate.toString(),
                    String.valueOf(done));
        }

        static String escape(String value) {
            return value.replace("\\", "\\\\").replace(",", "\\,");
        }
    }

    enum Priority {
        HIGH("高"), MEDIUM("中"), LOW("低");

        final String label;

        Priority(String label) {
            this.label = label;
        }

        static Priority parse(String raw) {
            String v = raw.trim().toUpperCase();
            switch (v) {
                case "H":
                case "HIGH":
                case "高":
                    return HIGH;
                case "L":
                case "LOW":
                case "低":
                    return LOW;
                default:
                    return MEDIUM;
            }
        }
    }

    private static final Path STORE =
            Paths.get(System.getProperty("user.home"), ".todo-java", "tasks.csv");

    private final List<Task> tasks = new ArrayList<>();
    private int nextId = 1;

    public static void main(String[] args) {
        new TaskManager().run();
    }

    private void run() {
        load();
        System.out.println("=== 待办事项管理器 ===");
        printHelp();

        try (Scanner scanner = new Scanner(System.in)) {
            while (true) {
                System.out.print("\n> ");
                if (!scanner.hasNextLine()) {
                    break;
                }
                String line = scanner.nextLine().trim();
                if (line.isEmpty()) {
                    continue;
                }

                String[] parts = line.split("\\s+", 2);
                String cmd = parts[0].toLowerCase();
                String arg = parts.length > 1 ? parts[1].trim() : "";

                switch (cmd) {
                    case "add":
                        addTask(scanner, arg);
                        break;
                    case "list":
                        listTasks(arg);
                        break;
                    case "done":
                        markDone(arg);
                        break;
                    case "del":
                    case "rm":
                        deleteTask(arg);
                        break;
                    case "find":
                        findTasks(arg);
                        break;
                    case "stats":
                        showStats();
                        break;
                    case "help":
                        printHelp();
                        break;
                    case "quit":
                    case "exit":
                        save();
                        System.out.println("已保存，再见！");
                        return;
                    default:
                        System.out.println("未知命令：" + cmd + "（输入 help 查看用法）");
                }
            }
        }
    }

    private void printHelp() {
        System.out.println("命令：");
        System.out.println("  add              新增待办（依次输入标题、优先级、截止日期）");
        System.out.println("  list [all|todo]  列出任务，默认只显示未完成");
        System.out.println("  done <id>        标记完成");
        System.out.println("  del <id>         删除任务");
        System.out.println("  find <关键字>     搜索标题");
        System.out.println("  stats            统计概览");
        System.out.println("  quit             保存并退出");
    }

    private void addTask(Scanner scanner, String inlineTitle) {
        String title = inlineTitle;
        if (title.isEmpty()) {
            System.out.print("标题：");
            title = scanner.nextLine().trim();
        }
        if (title.isEmpty()) {
            System.out.println("标题不能为空。");
            return;
        }

        System.out.print("优先级（高/中/低，默认中）：");
        String rawPriority = scanner.nextLine().trim();
        Priority priority = rawPriority.isEmpty() ? Priority.MEDIUM : Priority.parse(rawPriority);

        LocalDate due = null;
        System.out.print("截止日期（YYYY-MM-DD，可留空）：");
        String rawDate = scanner.nextLine().trim();
        if (!rawDate.isEmpty()) {
            try {
                due = LocalDate.parse(rawDate);
            } catch (DateTimeParseException e) {
                System.out.println("日期格式不合法，已忽略。");
            }
        }

        tasks.add(new Task(nextId++, title, priority, due, false));
        save();
        System.out.println("已添加任务 #" + (nextId - 1));
    }

    private void listTasks(String mode) {
        boolean showAll = mode.equalsIgnoreCase("all");
        List<Task> view = new ArrayList<>();
        for (Task t : tasks) {
            if (showAll || !t.done) {
                view.add(t);
            }
        }
        if (view.isEmpty()) {
            System.out.println(showAll ? "还没有任何任务。" : "没有未完成的任务，休息一下吧。");
            return;
        }

        view.sort(Comparator
                .comparing((Task t) -> t.priority.ordinal())
                .thenComparing(t -> t.dueDate == null ? LocalDate.MAX : t.dueDate)
                .thenComparingInt(t -> t.id));

        System.out.printf("%-5s %-6s %-5s %-12s %s%n", "ID", "状态", "优先级", "截止日期", "标题");
        System.out.println("-".repeat(70));
        for (Task t : view) {
            System.out.printf("%-5d %-6s %-5s %-12s %s%n",
                    t.id,
                    t.done ? "✓" : "○",
                    t.priority.label,
                    t.dueDate == null ? "-" : t.dueDate.toString(),
                    t.title);
        }
    }

    private void markDone(String arg) {
        Task task = findById(arg);
        if (task == null) {
            return;
        }
        task.done = !task.done;
        save();
        System.out.println("任务 #" + task.id + " 已标记为" + (task.done ? "完成" : "未完成"));
    }

    private void deleteTask(String arg) {
        Task task = findById(arg);
        if (task == null) {
            return;
        }
        tasks.remove(task);
        save();
        System.out.println("已删除任务 #" + task.id + "：" + task.title);
    }

    private void findTasks(String keyword) {
        if (keyword.isEmpty()) {
            System.out.println("请输入搜索关键字。");
            return;
        }
        String lower = keyword.toLowerCase();
        boolean hit = false;
        for (Task t : tasks) {
            if (t.title.toLowerCase().contains(lower)) {
                System.out.printf("#%d [%s] %s%n", t.id, t.done ? "✓" : "○", t.title);
                hit = true;
            }
        }
        if (!hit) {
            System.out.println("没有匹配的任务。");
        }
    }

    private void showStats() {
        long total = tasks.size();
        long done = tasks.stream().filter(t -> t.done).count();
        long overdue = tasks.stream()
                .filter(t -> !t.done && t.dueDate != null && t.dueDate.isBefore(LocalDate.now()))
                .count();
        double rate = total == 0 ? 0 : done * 100.0 / total;

        System.out.println("总任务数：" + total);
        System.out.println("已完成  ：" + done);
        System.out.println("未完成  ：" + (total - done));
        System.out.println("已逾期  ：" + overdue);
        System.out.printf("完成率  ：%.1f%%%n", rate);
        for (Priority p : Priority.values()) {
            long n = tasks.stream().filter(t -> t.priority == p && !t.done).count();
            System.out.println("  " + p.label + "优先级未完成：" + n);
        }
    }

    private Task findById(String arg) {
        int id;
        try {
            id = Integer.parseInt(arg.trim());
        } catch (NumberFormatException e) {
            System.out.println("请输入数字 ID，例如：done 3");
            return null;
        }
        for (Task t : tasks) {
            if (t.id == id) {
                return t;
            }
        }
        System.out.println("找不到 ID 为 " + id + " 的任务。");
        return null;
    }

    /** 从 CSV 加载任务。 */
    private void load() {
        if (!Files.exists(STORE)) {
            return;
        }
        try {
            for (String line : Files.readAllLines(STORE, StandardCharsets.UTF_8)) {
                if (line.trim().isEmpty()) {
                    continue;
                }
                String[] f = splitCsv(line);
                int id = Integer.parseInt(f[0]);
                LocalDate due = f[3].isEmpty() ? null : LocalDate.parse(f[3]);
                tasks.add(new Task(id, f[1], Priority.parse(f[2]), due, Boolean.parseBoolean(f[4])));
                nextId = Math.max(nextId, id + 1);
            }
        } catch (IOException | RuntimeException e) {
            System.out.println("读取历史任务失败，将以空列表启动：" + e.getMessage());
        }
    }

    private void save() {
        try {
            Files.createDirectories(STORE.getParent());
            List<String> lines = new ArrayList<>();
            for (Task t : tasks) {
                lines.add(t.toCsv());
            }
            Files.write(STORE, lines, StandardCharsets.UTF_8);
        } catch (IOException e) {
            System.out.println("保存失败：" + e.getMessage());
        }
    }

    /** 简易 CSV 切分，支持被转义的逗号。 */
    private static String[] splitCsv(String line) {
        List<String> out = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean escape = false;
        for (char c : line.toCharArray()) {
            if (escape) {
                cur.append(c);
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == ',') {
                out.add(cur.toString());
                cur.setLength(0);
            } else {
                cur.append(c);
            }
        }
        out.add(cur.toString());
        while (out.size() < 5) {
            out.add("");
        }
        return out.subList(0, 5).toArray(new String[0]);
    }
}
