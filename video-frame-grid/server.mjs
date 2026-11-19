import express from "express";
import multer from "multer";
import ffmpegPath from "ffmpeg-static";
import ffprobe from "ffprobe-static";
import { spawn } from "node:child_process";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const app = express();
const port = Number(process.env.PORT) || 4173;
const tempRoot = await fs.mkdtemp(path.join(os.tmpdir(), "video-frame-grid-"));
const jobs = new Map();
let activeJob = null;

const upload = multer({
  dest: tempRoot,
  limits: { fileSize: 2 * 1024 * 1024 * 1024 },
});

function valueInRange(value, min, max) {
  const number = Number(value);
  return Number.isInteger(number) && number >= min && number <= max
    ? number
    : null;
}

function validSpacing(value) {
  return (
    Number.isFinite(value) &&
    value >= 0 &&
    value <= 4 &&
    Number.isInteger(value * 4)
  );
}

function videoAspect(stream) {
  const [width, height] = String(stream?.display_aspect_ratio || "")
    .split(":")
    .map(Number);
  return width > 0 && height > 0
    ? width / height
    : stream.width / stream.height;
}

function sheetLayout(job) {
  const gap = Math.round((job.outputSize * job.spacingPercent) / 100);
  const maxWidth = Math.floor(
    (job.outputSize - (job.columns + 1) * gap) / job.columns,
  );
  const maxHeight = Math.floor(
    (job.outputSize - (job.rows + 1) * gap) / job.rows,
  );
  const cellWidth = Math.min(maxWidth, Math.floor(maxHeight * job.aspect));
  const cellHeight = Math.min(maxHeight, Math.floor(cellWidth / job.aspect));
  const mosaicWidth = job.columns * cellWidth + (job.columns + 1) * gap;
  const mosaicHeight = job.rows * cellHeight + (job.rows + 1) * gap;
  return {
    gap,
    cellWidth,
    cellHeight,
    offsetX: Math.floor((job.outputSize - mosaicWidth) / 2),
    offsetY: Math.floor((job.outputSize - mosaicHeight) / 2),
  };
}

function publicJob(job) {
  return {
    id: job.id,
    name: job.name,
    status: job.status,
    error: job.error,
    rows: job.rows,
    columns: job.columns,
    outputSize: job.outputSize,
    spacingPercent: job.spacingPercent,
    format: job.format,
    frameCount: job.frameCount,
    sampledFrames: job.sampledFrames,
    removedFrames: job.removedFrames,
    revision: job.revision,
    completedFrames: job.completedFrames,
    duration: job.duration,
    width: job.width,
    height: job.height,
    layout: job.layout,
  };
}

function run(command, args, job, onLine, onStderrLine) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { windowsHide: true });
    job.process = child;
    let output = "";
    let progress = "";
    let stderrProgress = "";
    child.stdout.on("data", (chunk) => {
      const data = chunk.toString();
      output += data;
      if (onLine && output.length > 16000) output = output.slice(-16000);
      if (onLine) {
        progress += data;
        const lines = progress.split(/\r?\n/);
        progress = lines.pop();
        for (const line of lines) onLine(line);
      }
    });
    child.stderr.on("data", (chunk) => {
      const data = chunk.toString();
      output += data;
      if (output.length > 16000) output = output.slice(-16000);
      if (onStderrLine) {
        stderrProgress += data;
        const lines = stderrProgress.split(/\r?\n/);
        stderrProgress = lines.pop();
        for (const line of lines) onStderrLine(line);
      }
    });
    child.on("error", reject);
    child.on("close", (code) => {
      job.process = null;
      if (code === 0) resolve(output);
      else reject(new Error(output.slice(-1800) || `进程退出，代码 ${code}`));
    });
  });
}

function sampleFrames(frameCount, capacity) {
  const count = Math.min(frameCount, capacity);
  if (count === 1) return [Math.round((frameCount - 1) / 2)];
  return Array.from({ length: count }, (_, index) =>
    Math.round((index * (frameCount - 1)) / (count - 1)),
  );
}

async function processJob(job) {
  try {
    if (!job.frameCount) {
      job.status = "probing";
      const probeOutput = await run(
        ffprobe.path,
        [
          "-v",
          "error",
          "-select_streams",
          "v:0",
          "-count_frames",
          "-show_entries",
          "stream=nb_read_frames,width,height,display_aspect_ratio:format=duration",
          "-of",
          "json",
          job.uploadPath,
        ],
        job,
      );
      if (job.cancelled) return;
      const info = JSON.parse(probeOutput);
      const stream = info.streams?.[0];
      job.frameCount = Number(stream?.nb_read_frames) || 0;
      job.duration = Number(info.format?.duration) || 0;
      job.width = stream?.width || 0;
      job.height = stream?.height || 0;
      job.aspect = videoAspect(stream);
      if (!job.frameCount)
        throw new Error("未检测到可读取的视频帧，请换一个视频文件重试。");
    }
    const indices = sampleFrames(job.frameCount, job.rows * job.columns);
    job.sampledFrames = indices.map((index) => index + 1);
    const selectedIndices = indices.filter(
      (index) => !job.removedFrames.includes(index + 1),
    );
    if (!selectedIndices.length)
      throw new Error("至少保留一帧后才能生成拼图。");

    job.status = "processing";
    const tile = `${job.columns}x${job.rows}`;
    job.layout = sheetLayout(job);
    const { gap, cellWidth, cellHeight } = job.layout;
    const selection = selectedIndices
      .map((index) => `eq(n\\,${index})`)
      .join("+");
    const filter = [
      `select=${selection}`,
      "showinfo",
      `scale=${cellWidth}:${cellHeight}:flags=lanczos:force_original_aspect_ratio=decrease:force_divisible_by=2`,
      `pad=${cellWidth}:${cellHeight}:(ow-iw)/2:(oh-ih)/2:color=0x1a2529`,
      `tile=${tile}:nb_frames=${job.rows * job.columns}:padding=${gap}:margin=${gap}:color=0x1a2529`,
      `pad=${job.outputSize}:${job.outputSize}:(ow-iw)/2:(oh-ih)/2:color=0x1a2529`,
    ].join(",");
    const args = [
      "-hide_banner",
      "-y",
      "-loglevel",
      "info",
      "-i",
      job.uploadPath,
      "-map",
      "0:v:0",
      "-vf",
      filter,
      "-an",
      "-fps_mode",
      "passthrough",
      "-frames:v",
      "1",
    ];
    if (job.format === "jpg") args.push("-q:v", "3");
    args.push(job.imagePath);
    await run(ffmpegPath, args, job, null, (line) => {
      if (/Parsed_showinfo.*\bn:\s*\d+\s+pts:/.test(line))
        job.completedFrames++;
    });
    if (job.cancelled) return;
    if ((await fs.stat(job.imagePath)).size === 0)
      throw new Error("拼图生成失败，请重试。");
    job.completedFrames = selectedIndices.length;
    job.revision++;
    job.status = "done";
  } catch (error) {
    if (!job.cancelled) {
      console.error("Video processing failed:", error);
      job.status = "error";
      job.error = error.message.includes("Invalid data")
        ? "视频文件无法读取，请检查格式或文件是否损坏。"
        : error.message;
    }
  } finally {
    if (job.cancelled) {
      await fs.rm(job.uploadPath, { force: true });
      await fs.rm(job.dir, { recursive: true, force: true });
    }
    if (activeJob === job) activeJob = null;
  }
}

app.use(express.static(path.join(here, "public")));
app.use(express.json());
app.use(
  "/lucide",
  express.static(path.join(here, "node_modules", "lucide", "dist", "umd")),
);

app.post(
  "/api/jobs",
  (request, response, next) => {
    if (activeJob)
      return response
        .status(409)
        .json({ error: "已有视频正在处理，请稍后再试。" });
    next();
  },
  upload.single("video"),
  async (request, response) => {
    if (activeJob) {
      if (request.file) await fs.rm(request.file.path, { force: true });
      return response
        .status(409)
        .json({ error: "已有视频正在处理，请稍后再试。" });
    }
    const rows = valueInRange(request.body.rows, 1, 10);
    const columns = valueInRange(request.body.columns, 1, 10);
    const outputSize = Number(request.body.outputSize);
    const spacingPercent = Number(request.body.spacingPercent);
    const format = request.body.format;
    if (
      !request.file ||
      !rows ||
      !columns ||
      ![512, 1024, 2048, 4096].includes(outputSize) ||
      !validSpacing(spacingPercent) ||
      !["jpg", "png"].includes(format)
    ) {
      if (request.file) await fs.rm(request.file.path, { force: true });
      return response
        .status(400)
        .json({ error: "请提供视频，并检查网格和图片设置。" });
    }
    const id = randomUUID();
    const dir = path.join(tempRoot, id);
    await fs.mkdir(dir);
    const job = {
      id,
      dir,
      imagePath: path.join(dir, `contact-sheet.${format}`),
      uploadPath: request.file.path,
      name: path.basename(request.file.originalname),
      rows,
      columns,
      outputSize,
      spacingPercent,
      format,
      status: "queued",
      error: null,
      frameCount: 0,
      sampledFrames: [],
      removedFrames: [],
      revision: 0,
      completedFrames: 0,
      duration: 0,
      width: 0,
      height: 0,
      createdAt: Date.now(),
      cancelled: false,
      process: null,
    };
    jobs.set(id, job);
    activeJob = job;
    response.status(202).json(publicJob(job));
    void processJob(job);
  },
);

app.get("/api/jobs/:id", (request, response) => {
  const job = jobs.get(request.params.id);
  if (!job)
    return response.status(404).json({ error: "结果已过期，请重新处理。" });
  response.json(publicJob(job));
});

app.patch("/api/jobs/:id/frames", async (request, response) => {
  const job = jobs.get(request.params.id);
  if (!job)
    return response.status(404).json({ error: "结果已过期，请重新处理。" });
  if (activeJob)
    return response
      .status(409)
      .json({ error: "已有视频正在处理，请稍后再试。" });
  if (job.status !== "done")
    return response.status(409).json({ error: "当前结果还未准备好。" });
  const removedFrames = Array.isArray(request.body?.removedFrames)
    ? [...new Set(request.body.removedFrames.map(Number))]
    : null;
  const valid = removedFrames?.every(
    (frame) => Number.isInteger(frame) && job.sampledFrames.includes(frame),
  );
  if (!valid || removedFrames.length >= job.sampledFrames.length) {
    return response.status(400).json({ error: "至少保留一帧后才能生成拼图。" });
  }
  job.removedFrames = removedFrames;
  startReprocess(job, response);
});

function startReprocess(job, response) {
  job.completedFrames = 0;
  job.error = null;
  job.status = "queued";
  activeJob = job;
  response.status(202).json(publicJob(job));
  void processJob(job);
}

app.patch("/api/jobs/:id/spacing", (request, response) => {
  const job = jobs.get(request.params.id);
  if (!job)
    return response.status(404).json({ error: "结果已过期，请重新处理。" });
  if (activeJob || job.status !== "done")
    return response.status(409).json({ error: "当前结果还未准备好。" });
  const spacingPercent = Number(request.body?.spacingPercent);
  if (!validSpacing(spacingPercent))
    return response.status(400).json({ error: "间距应在 0% 到 4% 之间。" });
  job.spacingPercent = spacingPercent;
  startReprocess(job, response);
});

app.get("/api/jobs/:id/image", (request, response) => {
  const job = jobs.get(request.params.id);
  if (!job || job.status !== "done") return response.status(404).end();
  if (request.query.download === "1")
    return response.download(job.imagePath, `contact-sheet.${job.format}`);
  response.sendFile(job.imagePath);
});

app.delete("/api/jobs/:id", async (request, response) => {
  const job = jobs.get(request.params.id);
  if (!job) return response.status(404).end();
  const finished = ["done", "error"].includes(job.status);
  job.cancelled = true;
  job.status = "cancelled";
  job.process?.kill();
  jobs.delete(job.id);
  if (activeJob === job) activeJob = null;
  if (finished) {
    await fs.rm(job.uploadPath, { force: true });
    await fs.rm(job.dir, { recursive: true, force: true });
  }
  response.status(204).end();
});

app.use((error, request, response, next) => {
  if (error instanceof multer.MulterError && error.code === "LIMIT_FILE_SIZE") {
    return response.status(413).json({ error: "视频不能超过 2 GB。" });
  }
  console.error(error);
  response.status(500).json({ error: "处理失败，请重试。" });
});

setInterval(
  async () => {
    for (const job of jobs.values()) {
      if (job === activeJob || Date.now() - job.createdAt < 60 * 60 * 1000)
        continue;
      jobs.delete(job.id);
      await fs.rm(job.dir, { recursive: true, force: true });
      await fs.rm(job.uploadPath, { force: true });
    }
  },
  10 * 60 * 1000,
).unref();

app.listen(port, "127.0.0.1", () =>
  console.log(`Video frame grid: http://localhost:${port}`),
);
