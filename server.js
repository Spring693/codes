#!/usr/bin/env node
/**
 * 轻量级静态文件服务器（仅用 Node 内置模块，零依赖）。
 *
 * 功能：把本地目录发布为 HTTP 服务，支持 MIME 推断、目录列表、
 *       路径穿越防护、Range 请求与访问日志。
 *
 * 用法：
 *   node server.js [根目录] [端口]
 *   node server.js ./public 8080
 */

"use strict";

const http = require("http");
const fs = require("fs");
const fsp = require("fs/promises");
const path = require("path");
const os = require("os");

const ROOT = path.resolve(process.argv[2] || ".");
const PORT = Number(process.argv[3] || process.env.PORT || 8080);

const MIME_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".htm": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
  ".pdf": "application/pdf",
  ".txt": "text/plain; charset=utf-8",
  ".md": "text/markdown; charset=utf-8",
  ".mp4": "video/mp4",
  ".woff2": "font/woff2",
};

function contentTypeFor(filePath) {
  return MIME_TYPES[path.extname(filePath).toLowerCase()] ||
    "application/octet-stream";
}

function formatSize(bytes) {
  const units = ["B", "KB", "MB", "GB"];
  let size = bytes;
  let unit = 0;
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024;
    unit += 1;
  }
  return `${size.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function safeResolve(urlPath) {
  // 解码后拼接，再确认最终路径没有跳出根目录
  let decoded;
  try {
    decoded = decodeURIComponent(urlPath.split("?")[0]);
  } catch {
    return null;
  }
  const target = path.resolve(ROOT, "." + path.posix.normalize(decoded));
  if (target !== ROOT && !target.startsWith(ROOT + path.sep)) {
    return null;
  }
  return target;
}

function sendError(res, status, message) {
  const body = `<!doctype html><meta charset="utf-8">
<h1>${status} ${message}</h1><p>Static Server</p>`;
  res.writeHead(status, {
    "Content-Type": "text/html; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(body);
}

async function sendDirectory(res, dirPath, urlPath) {
  const entries = await fsp.readdir(dirPath, { withFileTypes: true });
  const rows = await Promise.all(
    entries.map(async (entry) => {
      const full = path.join(dirPath, entry.name);
      let stat = null;
      try {
        stat = await fsp.stat(full);
      } catch {
        stat = null;
      }
      const isDir = entry.isDirectory();
      const href = path.posix.join(urlPath, encodeURIComponent(entry.name));
      return `<tr>
        <td><a href="${href}${isDir ? "/" : ""}">${entry.name}${isDir ? "/" : ""}</a></td>
        <td>${isDir ? "-" : formatSize(stat ? stat.size : 0)}</td>
        <td>${stat ? stat.mtime.toISOString().slice(0, 19).replace("T", " ") : "-"}</td>
      </tr>`;
    })
  );

  const html = `<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"><title>目录：${urlPath}</title>
<style>body{font-family:system-ui,sans-serif;margin:2rem}
table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #eee;padding:.4rem .6rem;text-align:left}</style>
</head><body><h1>目录：${urlPath}</h1>
<table><thead><tr><th>名称</th><th>大小</th><th>修改时间</th></tr></thead>
<tbody>${rows.join("\n")}</tbody></table></body></html>`;

  res.writeHead(200, {
    "Content-Type": "text/html; charset=utf-8",
    "Content-Length": Buffer.byteLength(html),
  });
  res.end(html);
}

async function sendFile(res, filePath, stat, rangeHeader) {
  const type = contentTypeFor(filePath);
  const baseHeaders = {
    "Content-Type": type,
    "Accept-Ranges": "bytes",
    "Last-Modified": stat.mtime.toUTCString(),
  };

  // 简单支持单段 Range 请求（视频/音频拖拽播放需要）
  if (rangeHeader) {
    const match = /bytes=(\d*)-(\d*)/.exec(rangeHeader);
    if (match) {
      const start = match[1] ? parseInt(match[1], 10) : 0;
      const end = match[2] ? parseInt(match[2], 10) : stat.size - 1;
      if (start >= 0 && end < stat.size && start <= end) {
        res.writeHead(206, {
          ...baseHeaders,
          "Content-Length": end - start + 1,
          "Content-Range": `bytes ${start}-${end}/${stat.size}`,
        });
        fs.createReadStream(filePath, { start, end }).pipe(res);
        return;
      }
      res.writeHead(416, { "Content-Range": `bytes */${stat.size}` });
      res.end();
      return;
    }
  }

  res.writeHead(200, { ...baseHeaders, "Content-Length": stat.size });
  fs.createReadStream(filePath).pipe(res);
}

const server = http.createServer(async (req, res) => {
  if (req.method !== "GET" && req.method !== "HEAD") {
    sendError(res, 405, "Method Not Allowed");
    return;
  }

  let urlPath = req.url === "/" ? "/" : req.url.split("?")[0];
  if (urlPath.endsWith("/")) {
    urlPath = urlPath.slice(0, -1);
  }

  const target = safeResolve(urlPath || "/");
  if (!target) {
    sendError(res, 403, "Forbidden");
    return;
  }

  try {
    const stat = await fsp.stat(target);
    if (stat.isDirectory()) {
      const indexPath = path.join(target, "index.html");
      const indexStat = await fsp.stat(indexPath).catch(() => null);
      if (indexStat && indexStat.isFile()) {
        await sendFile(res, indexPath, indexStat, req.headers.range);
      } else {
        await sendDirectory(res, target, urlPath || "/");
      }
    } else {
      await sendFile(res, target, stat, req.headers.range);
    }
  } catch {
    sendError(res, 404, "Not Found");
  }

  res.on("finish", () => {
    const now = new Date().toISOString().slice(11, 19);
    console.log(`[${now}] ${res.statusCode} ${req.method} ${req.url}`);
  });
});

server.listen(PORT, () => {
  const nets = os.networkInterfaces();
  const lan = Object.values(nets)
    .flat()
    .find((n) => n && n.family === "IPv4" && !n.internal);
  console.log(`静态服务器已启动，根目录：${ROOT}`);
  console.log(`本机访问：http://localhost:${PORT}/`);
  if (lan) {
    console.log(`局域网访问：http://${lan.address}:${PORT}/`);
  }
  console.log("按 Ctrl+C 停止。");
});
