import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const format = "git-uncommitted-byte-snapshot";
const digest = (bytes) => crypto.createHash("sha256").update(bytes).digest("hex");
const git = (root, args, input, env = {}) => execFileSync("git", ["-C", root, "--literal-pathspecs", ...args], {
  input, env: { ...process.env, GIT_OPTIONAL_LOCKS: "0", ...env },
  maxBuffer: 256 * 1024 * 1024, windowsHide: true,
});
const records = (buffer) => buffer.toString("utf8").split("\0").filter(Boolean);
const status = (root) => git(root, ["status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames"]);
const head = (root) => git(root, ["rev-parse", "HEAD"]).toString().trim();

function safePath(root, relative) {
  if (typeof relative !== "string" || !relative || relative.includes("\\") || relative.includes(":")) throw new Error("Invalid snapshot path");
  const segments = relative.split("/");
  if (segments.some((part) => !part || part === "." || part === ".." || /^\.git$/i.test(part))) throw new Error("Unsafe snapshot path");
  const target = path.resolve(root, ...segments);
  const inside = path.relative(root, target);
  if (inside.startsWith("..") || path.isAbsolute(inside)) throw new Error("Path escapes target repository");
  for (let i = 1; i < segments.length; i++) {
    const parent = path.resolve(root, ...segments.slice(0, i));
    if (fs.existsSync(parent) && (fs.lstatSync(parent).isSymbolicLink() || !fs.lstatSync(parent).isDirectory())) throw new Error("Unsafe parent: " + parent);
  }
  return target;
}

function readWorking(root, relative, mode) {
  const target = safePath(root, relative);
  let info;
  try { info = fs.lstatSync(target); } catch (error) { if (error.code === "ENOENT") return null; throw error; }
  if (info.isSymbolicLink()) return { bytes: Buffer.from(fs.readlinkSync(target), "utf8"), mode: "120000" };
  if (!info.isFile()) throw new Error("Unsupported file type: " + relative);
  return { bytes: fs.readFileSync(target), mode: mode === "100755" ? "100755" : "100644" };
}

function treeEntry(root, relative) {
  const row = records(git(root, ["ls-tree", "-z", "HEAD", "--", relative])).find((row) => row.slice(row.indexOf("\t") + 1) === relative);
  if (!row) return null;
  const [mode, type, oid] = row.slice(0, row.indexOf("\t")).split(" ");
  if (type !== "blob") throw new Error("Submodules/directories are not supported: " + relative);
  return { mode, oid, bytes: git(root, ["cat-file", "blob", oid]) };
}

function indexEntry(root, relative) {
  const rows = records(git(root, ["ls-files", "--stage", "-z", "--", relative])).filter((row) => row.slice(row.indexOf("\t") + 1) === relative);
  if (!rows.length) return null;
  if (rows.length !== 1) throw new Error("Resolve Git conflicts before exporting: " + relative);
  const [mode, oid, stage] = rows[0].slice(0, rows[0].indexOf("\t")).split(" ");
  if (stage !== "0" || !["100644", "100755", "120000"].includes(mode) || /^0+$/.test(oid)) throw new Error("Unsupported index entry: " + relative);
  return { mode, oid, bytes: git(root, ["cat-file", "blob", oid]) };
}

function exportSnapshot(root, destination) {
  root = fs.realpathSync(root);
  const baseCommit = head(root);
  const before = status(root);
  const blobs = {};
  const pack = (entry) => {
    if (!entry) return null;
    const sha256 = digest(entry.bytes);
    if (!blobs[sha256]) {
      const blob = { encoding: "base64", byteLength: entry.bytes.length, data: entry.bytes.toString("base64") };
      const text = entry.bytes.toString("utf8");
      if (!text.includes("\0") && Buffer.from(text, "utf8").equals(entry.bytes)) blob.textUtf8 = text;
      blobs[sha256] = blob;
    }
    return { mode: entry.mode, sha256, byteLength: entry.bytes.length, ...(entry.oid ? { gitBlobOid: entry.oid } : {}) };
  };
  const files = records(before).map((row) => {
    const relative = row.slice(3);
    const base = treeEntry(root, relative);
    const index = indexEntry(root, relative);
    const working = readWorking(root, relative, index?.mode || base?.mode);
    const attrs = records(git(root, ["check-attr", "-z", "filter", "--", relative]));
    return {
      path: relative, gitStatus: row.slice(0, 2),
      group: relative.startsWith("Content/") ? "UE关卡与资产" : "其他项目文件",
      gitFilter: attrs[2] ?? "unspecified",
      base: pack(base), index: pack(index), workingTree: pack(working),
    };
  });
  assert.equal(new Set(files.map((file) => file.path)).size, files.length);
  assert.deepEqual(status(root), before, "Git status changed during export");
  assert.equal(head(root), baseCommit, "HEAD changed during export");
  for (const file of files) {
    const working = readWorking(root, file.path, file.workingTree?.mode);
    assert.equal(working ? digest(working.bytes) : null, file.workingTree?.sha256 ?? null, "File changed during export: " + file.path);
    assert.equal(indexEntry(root, file.path)?.oid ?? null, file.index?.gitBlobOid ?? null, "Index changed during export: " + file.path);
  }
  const snapshot = {
    format, version: 1, createdAtUtc: new Date().toISOString(),
    source: { repositoryName: path.basename(root), repositoryPath: root, baseCommit, objectFormat: git(root, ["rev-parse", "--show-object-format"]).toString().trim() },
    scope: {
      includes: ["已暂存改动", "未暂存改动", "未跟踪且未被Git忽略的文件", "新增、修改、删除及文件模式变化", "每个变化文件的HEAD原文、暂存区字节和工作区字节"],
      excludes: ["未变化的基准仓库文件", "Git忽略文件及构建缓存", "UE编辑器内未保存的改动", "Git历史、分支与远程配置", "时间戳、ACL及Git索引内部缓存"],
      guarantee: "在相同基准提交和依赖环境上，按字节恢复所有记录文件及其暂存状态；不是脱离基准仓库即可运行的完整项目，也不是UE蓝图或关卡的语义JSON。",
    },
    summary: {
      changedFiles: files.length,
      stagedFiles: files.filter((file) => file.gitStatus !== "??" && file.gitStatus[0] !== " ").length,
      unstagedFiles: files.filter((file) => file.gitStatus !== "??" && file.gitStatus[1] !== " ").length,
      untrackedFiles: files.filter((file) => file.gitStatus === "??").length,
      ueAssetFiles: files.filter((file) => /\.(uasset|umap)$/i.test(file.path)).length,
      workingTreeBytes: files.reduce((total, file) => total + (file.workingTree?.byteLength || 0), 0),
      uniqueContentBlobs: Object.keys(blobs).length,
    },
    restoration: {
      requirements: ["目标仓库HEAD必须等于source.baseCommit", "目标仓库无已暂存、未暂存或未跟踪改动", "关闭目标项目的UE编辑器以避免覆盖", "使用Git与Node.js 18或更新版本运行配套工具"],
      steps: [
        "先校验JSON格式、相对路径以及blobs中所有Base64内容的SHA-256和byteLength。",
        "各文件base/index/workingTree引用blobs[sha256]；null表示该层不存在。所有data均为文件原始字节，不进行换行转换。",
        "在相同HEAD的干净目标仓库中，workingTree为null时删除文件，否则Base64解码后按原路径写入；120000代表符号链接。",
        "逐个将index引用的字节通过git hash-object -w --stdin写入对象库，然后git update-index --add --cacheinfo恢复文件模式及暂存内容；index为null时只移除索引项、不删除工作区文件。不要使用git add，以免工作区与暂存区不同或触发过滤器。",
        "检查目标Git状态与files.gitStatus一致，并核对目标工作区和暂存区字节的SHA-256。",
      ],
      helper: "Ori5_8_snapshot.mjs",
      commandArguments: ["node", "Ori5_8_snapshot.mjs", "restore", "Ori5_8_uncommitted.json", "目标仓库绝对路径"],
    },
    files, blobs,
  };
  fs.writeFileSync(destination, JSON.stringify(snapshot, null, 2) + "\n", { flag: "wx" });
  console.log(JSON.stringify({ output: destination, bytes: fs.statSync(destination).size, ...snapshot.summary }, null, 2));
}

function loadSnapshot(filename) {
  const snapshot = JSON.parse(fs.readFileSync(filename, "utf8"));
  assert.equal(snapshot.format, format);
  assert.equal(snapshot.version, 1);
  assert.match(snapshot.source.baseCommit, /^[a-f0-9]{40}(?:[a-f0-9]{24})?$/);
  assert.equal(new Set(snapshot.files.map((file) => file.path.toLowerCase())).size, snapshot.files.length);
  for (const [sha256, blob] of Object.entries(snapshot.blobs)) {
    assert.match(sha256, /^[a-f0-9]{64}$/);
    assert.equal(blob.encoding, "base64");
    const bytes = Buffer.from(blob.data, "base64");
    assert.equal(bytes.toString("base64"), blob.data);
    assert.equal(bytes.length, blob.byteLength);
    assert.equal(digest(bytes), sha256);
    if (blob.textUtf8 !== undefined) assert.deepEqual(Buffer.from(blob.textUtf8, "utf8"), bytes);
  }
  for (const file of snapshot.files) {
    for (const entry of [file.base, file.index, file.workingTree]) {
      if (!entry) continue;
      assert.ok(["100644", "100755", "120000"].includes(entry.mode));
      const blob = snapshot.blobs[entry.sha256];
      assert.ok(blob);
      assert.equal(blob.byteLength, entry.byteLength);
    }
  }
  assert.equal(snapshot.files.length, snapshot.summary.changedFiles);
  return snapshot;
}

const unpack = (snapshot, entry) => Buffer.from(snapshot.blobs[entry.sha256].data, "base64");

function assertRestored(snapshot, root) {
  for (const file of snapshot.files) {
    const working = readWorking(root, file.path, file.workingTree?.mode);
    assert.equal(working ? digest(working.bytes) : null, file.workingTree?.sha256 ?? null, "Working-tree mismatch: " + file.path);
    const index = indexEntry(root, file.path);
    assert.equal(index ? digest(index.bytes) : null, file.index?.sha256 ?? null, "Index mismatch: " + file.path);
    assert.equal(index?.mode ?? null, file.index?.mode ?? null);
  }
  const actual = records(status(root)).sort();
  const expected = snapshot.files.map((file) => file.gitStatus + " " + file.path).sort();
  assert.deepEqual(actual, expected, "Restored Git status differs");
}

function restoreSnapshot(snapshot, root) {
  root = fs.realpathSync(root);
  assert.equal(head(root), snapshot.source.baseCommit, "Target HEAD must match snapshot baseCommit");
  assert.equal(status(root).length, 0, "Target must be clean, including untracked files");
  for (const file of snapshot.files) {
    const target = safePath(root, file.path);
    if (fs.existsSync(target) && fs.lstatSync(target).isDirectory()) throw new Error("Target is a directory: " + target);
    if (!file.base && fs.existsSync(target)) throw new Error("Target would overwrite an ignored file: " + target);
    if (process.platform === "win32" && file.workingTree?.mode === "120000") throw new Error("Restore symbolic links on a platform with matching Git symlink support");
  }
  for (const file of snapshot.files) {
    const target = safePath(root, file.path);
    if (fs.existsSync(target)) fs.unlinkSync(target);
    if (file.workingTree) {
      fs.mkdirSync(path.dirname(target), { recursive: true });
      const bytes = unpack(snapshot, file.workingTree);
      if (file.workingTree.mode === "120000") fs.symlinkSync(bytes.toString("utf8"), target);
      else {
        fs.writeFileSync(target, bytes, { flag: "wx" });
        if (process.platform !== "win32") fs.chmodSync(target, file.workingTree.mode === "100755" ? 0o755 : 0o644);
      }
    }
    if (file.index) {
      const oid = git(root, ["hash-object", "-w", "--stdin"], unpack(snapshot, file.index)).toString().trim();
      git(root, ["update-index", "--add", "--cacheinfo", file.index.mode, oid, file.path]);
    } else git(root, ["update-index", "--force-remove", "--", file.path]);
  }
  assertRestored(snapshot, root);
}

function verifySnapshot(snapshot, root) {
  root = fs.realpathSync(root);
  assert.equal(head(root), snapshot.source.baseCommit);
  assertRestored(snapshot, root);
  const initialStatus = status(root);
  const initialIndex = fs.readFileSync(git(root, ["rev-parse", "--path-format=absolute", "--git-path", "index"]).toString().trim());
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), "ori58-snapshot-check-"));
  const worktree = path.join(temp, "worktree");
  const objectDir = path.join(temp, "objects");
  fs.mkdirSync(worktree);
  fs.mkdirSync(objectDir);
  const env = {
    GIT_DIR: git(root, ["rev-parse", "--absolute-git-dir"]).toString().trim(),
    GIT_WORK_TREE: worktree,
    GIT_INDEX_FILE: path.join(temp, "index"),
    GIT_OBJECT_DIRECTORY: objectDir,
    GIT_ALTERNATE_OBJECT_DIRECTORIES: git(root, ["rev-parse", "--path-format=absolute", "--git-path", "objects"]).toString().trim(),
  };
  const savedEnv = Object.fromEntries(Object.keys(env).map((key) => [key, process.env[key]]));
  try {
    Object.assign(process.env, env);
    git(worktree, ["read-tree", snapshot.source.baseCommit]);
    const tracked = git(worktree, ["ls-files", "-z"]);
    git(worktree, ["update-index", "--skip-worktree", "-z", "--stdin"], tracked);
    const changedBasePaths = snapshot.files.filter((file) => file.base).map((file) => file.path);
    if (changedBasePaths.length) {
      const input = Buffer.from(changedBasePaths.join("\0") + "\0");
      git(worktree, ["update-index", "--no-skip-worktree", "-z", "--stdin"], input);
      git(worktree, ["checkout-index", "-z", "--stdin"], input);
    }
    restoreSnapshot(snapshot, worktree);
    console.log(JSON.stringify({ verified: true, isolatedRestore: true, files: snapshot.files.length, contentBlobs: Object.keys(snapshot.blobs).length, baseCommit: snapshot.source.baseCommit }, null, 2));
  } finally {
    for (const [key, value] of Object.entries(savedEnv)) {
      if (value === undefined) delete process.env[key];
      else process.env[key] = value;
    }
    fs.rmSync(temp, { recursive: true, force: true });
  }
  assert.deepEqual(status(root), initialStatus, "Source Git status was changed");
  assert.deepEqual(fs.readFileSync(git(root, ["rev-parse", "--path-format=absolute", "--git-path", "index"]).toString().trim()), initialIndex, "Source index was changed");
  assertRestored(snapshot, root);
}

const [, , command, first, second] = process.argv;
try {
  if (command === "export" && first && second) exportSnapshot(first, path.resolve(second));
  else if (command === "verify" && first && second) verifySnapshot(loadSnapshot(first), second);
  else if (command === "restore" && first && second) {
    restoreSnapshot(loadSnapshot(first), second);
    console.log("Restored file bytes and Git staging state successfully.");
  } else throw new Error("Usage: node " + path.basename(fileURLToPath(import.meta.url)) + " export <repository> <new.json> | verify <snapshot.json> <source-repository> | restore <snapshot.json> <clean-target-repository>");
} catch (error) {
  console.error(error.stack);
  process.exitCode = 1;
}
