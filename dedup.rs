// dedup.rs — 目录重复文件查找工具（仅用标准库）。
//
// 功能：扫描目录，先按文件大小分组，再对同大小文件计算 64 位哈希，
//       找出内容完全相同的文件组，并估算可回收的空间。
//       默认只报告（--dry-run），加 --delete 才会删除重复项。
//
// 用法：
//   rustc -O dedup.rs -o dedup && ./dedup ~/Downloads
//   ./dedup ~/Photos --min-size 10240
//   ./dedup ~/Photos --delete          // 真正删除（保留每组中路径最短的一个）

use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::Instant;

/// 以流式方式计算文件内容的哈希，避免一次性把大文件读进内存。
fn hash_file(path: &Path) -> io::Result<u64> {
    let mut hasher = DefaultHasher::new();
    let mut file = fs::File::open(path)?;
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        // 混入字节数与内容，降低偶然碰撞概率
        read.hash(&mut hasher);
        buffer[..read].hash(&mut hasher);
    }
    Ok(hasher.finish())
}

/// 递归收集目录下的普通文件。
fn collect_files(root: &Path, files: &mut Vec<PathBuf>) -> io::Result<()> {
    if root.is_file() {
        files.push(root.to_path_buf());
        return Ok(());
    }
    for entry in fs::read_dir(root)? {
        let entry = entry?;
        let path = entry.path();
        let meta = match entry.metadata() {
            Ok(m) => m,
            Err(_) => continue,
        };
        if meta.is_dir() {
            collect_files(&path, files)?;
        } else if meta.is_file() {
            files.push(path);
        }
    }
    Ok(())
}

fn format_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut size = bytes as f64;
    let mut unit = 0;
    while size >= 1024.0 && unit < UNITS.len() - 1 {
        size /= 1024.0;
        unit += 1;
    }
    format!("{:.1} {}", size, UNITS[unit])
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let mut root: Option<PathBuf> = None;
    let mut min_size: u64 = 1;
    let mut do_delete = false;

    let mut iter = args.iter().skip(1);
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--min-size" => {
                min_size = iter
                    .next()
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(1);
            }
            "--delete" => do_delete = true,
            "-h" | "--help" => {
                println!("用法：dedup <目录> [--min-size 字节数] [--delete]");
                return;
            }
            other => {
                if root.is_none() && !other.starts_with('-') {
                    root = Some(PathBuf::from(other));
                }
            }
        }
    }

    let root = match root {
        Some(r) => r,
        None => {
            eprintln!("用法：dedup <目录> [--min-size 字节数] [--delete]");
            std::process::exit(1);
        }
    };

    if !root.exists() {
        eprintln!("路径不存在：{}", root.display());
        std::process::exit(1);
    }

    let started = Instant::now();
    let mut files: Vec<PathBuf> = Vec::new();
    if let Err(err) = collect_files(&root, &mut files) {
        eprintln!("扫描目录失败：{}", err);
        std::process::exit(1);
    }
    println!("扫描到 {} 个文件", files.len());

    // 第一步：按文件大小分组，只有大小相同的文件才可能重复
    let mut by_size: HashMap<u64, Vec<PathBuf>> = HashMap::new();
    for path in files {
        if let Ok(meta) = fs::metadata(&path) {
            let len = meta.len();
            if len >= min_size {
                by_size.entry(len).or_default().push(path);
            }
        }
    }

    let candidate_groups = by_size.values().filter(|v| v.len() > 1).count();
    println!("按大小初筛出 {} 组候选文件，正在计算内容哈希…", candidate_groups);

    // 第二步：对候选组计算哈希，确认内容是否真的相同
    let mut duplicates: HashMap<(u64, u64), Vec<PathBuf>> = HashMap::new();
    for (size, group) in by_size {
        if group.len() < 2 {
            continue;
        }
        for path in group {
            if let Ok(hash) = hash_file(&path) {
                duplicates.entry((size, hash)).or_default().push(path);
            }
        }
    }

    let mut wasted: u64 = 0;
    let mut group_no = 0;

    for ((size, _hash), mut group) in duplicates {
        if group.len() < 2 {
            continue;
        }
        group.sort();
        group_no += 1;
        wasted += size * (group.len() as u64 - 1);

        println!("\n[第 {} 组] {} × {} 份", group_no, format_size(size), group.len());
        for path in &group {
            println!("   {}", path.display());
        }

        if do_delete {
            // 保留路径最短（通常层级最浅）的一份，其余删除
            let keep = group[0].clone();
            for path in group.iter().skip(1) {
                match fs::remove_file(path) {
                    Ok(()) => println!("   已删除：{}", path.display()),
                    Err(err) => eprintln!("   删除失败 {}: {}", path.display(), err),
                }
            }
            println!("   保留：{}", keep.display());
        }
    }

    println!("\n{}", "-".repeat(60));
    if group_no == 0 {
        println!("未发现重复文件。");
    } else {
        println!("共发现 {} 组重复文件，可释放约 {}。", group_no, format_size(wasted));
        if !do_delete {
            println!("以上为预演结果，加 --delete 参数才会真正删除。");
        }
    }
    println!("耗时：{:?}", started.elapsed());
}
