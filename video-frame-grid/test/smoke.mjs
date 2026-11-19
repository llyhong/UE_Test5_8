import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ffmpegPath from "ffmpeg-static";
import ffprobe from "ffprobe-static";

const temp = await fs.mkdtemp(path.join(os.tmpdir(), "frame-grid-test-"));
const port = 43000 + Math.floor(Math.random() * 10000);
const base = `http://localhost:${port}`;
const server = spawn(process.execPath, ["server.mjs"], {
  cwd: path.resolve(path.dirname(fileURLToPath(import.meta.url)), ".."),
  env: { ...process.env, PORT: String(port) },
  windowsHide: true,
});

async function command(program, args, binary = false) {
  const child = spawn(program, args, { windowsHide: true });
  const stdout = [];
  let stderr = "";
  child.stdout.on("data", (chunk) => stdout.push(chunk));
  child.stderr.on("data", (chunk) => {
    stderr += chunk;
  });
  const [code] = await once(child, "close");
  assert.equal(code, 0, stderr);
  const output = Buffer.concat(stdout);
  return binary ? output : output.toString();
}

async function waitForServer() {
  for (let attempt = 0; attempt < 50; attempt++) {
    try {
      if ((await fetch(base)).ok) return;
    } catch {}
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error("Server did not start");
}

async function makeVideo(name, frames, size = "96x54") {
  const source = path.join(temp, name);
  await command(ffmpegPath, [
    "-hide_banner",
    "-loglevel",
    "error",
    "-f",
    "lavfi",
    "-i",
    `testsrc2=size=${size}:rate=10`,
    "-frames:v",
    String(frames),
    "-c:v",
    "mpeg4",
    source,
  ]);
  return source;
}

async function testGrid(
  source,
  rows,
  columns,
  format,
  outputSize,
  expectedFrames,
  expectedSamples,
  keep = false,
  spacingPercent = 0.75,
  expectedAspect = 16 / 9,
) {
  const data = new FormData();
  data.append(
    "video",
    new Blob([await fs.readFile(source)], { type: "video/mp4" }),
    path.basename(source),
  );
  data.append("rows", rows);
  data.append("columns", columns);
  data.append("outputSize", String(outputSize));
  data.append("spacingPercent", String(spacingPercent));
  data.append("format", format);
  const created = await fetch(`${base}/api/jobs`, {
    method: "POST",
    body: data,
  });
  assert.equal(created.status, 202);
  let job = await waitForJob((await created.json()).id);
  assert.equal(job.status, "done");
  assert.equal(job.outputSize, outputSize);
  assert.equal(job.spacingPercent, spacingPercent);
  assert.equal(job.layout.gap, Math.round((outputSize * spacingPercent) / 100));
  assert.ok(
    Math.abs(job.layout.cellWidth / job.layout.cellHeight - expectedAspect) <
      0.08,
  );
  if (expectedAspect > 1) assert.ok(job.layout.offsetY > job.layout.offsetX);
  if (expectedAspect < 1) assert.ok(job.layout.offsetX > job.layout.offsetY);
  assert.equal(job.frameCount, expectedFrames);
  assert.deepEqual(job.sampledFrames, expectedSamples);
  const image = await fetch(`${base}/api/jobs/${job.id}/image`);
  assert.equal(image.status, 200);
  assert.match(
    image.headers.get("content-type"),
    format === "jpg" ? /jpeg/ : /png/,
  );
  const imagePath = path.join(
    temp,
    `output-${rows}x${columns}-${outputSize}.${format}`,
  );
  const imageBytes = Buffer.from(await image.arrayBuffer());
  await fs.writeFile(imagePath, imageBytes);
  const dimensions = JSON.parse(
    await command(ffprobe.path, [
      "-v",
      "error",
      "-show_entries",
      "stream=width,height",
      "-of",
      "json",
      imagePath,
    ]),
  );
  assert.deepEqual(
    dimensions.streams.map(({ width, height }) => ({ width, height })),
    [{ width: outputSize, height: outputSize }],
  );
  if (format === "png" && job.layout.gap > 0) {
    const raw = await command(
      ffmpegPath,
      [
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        imagePath,
        "-frames:v",
        "1",
        "-pix_fmt",
        "rgb24",
        "-f",
        "rawvideo",
        "pipe:1",
      ],
      true,
    );
    const { gap, cellWidth, cellHeight, offsetX, offsetY } = job.layout;
    const firstX = offsetX + gap;
    const firstY = offsetY + gap;
    const pixel = (x, y) =>
      raw.subarray((y * outputSize + x) * 3, (y * outputSize + x) * 3 + 3);
    const samples = [0.2, 0.4, 0.6, 0.8].map((part) => {
      const y = firstY + Math.floor(cellHeight * part);
      const inside = pixel(firstX + Math.floor(cellWidth * part), y);
      const between = pixel(firstX + cellWidth + Math.floor(gap / 2), y);
      return inside.reduce(
        (sum, value, index) => sum + Math.abs(value - between[index]),
        0,
      );
    });
    assert.ok(
      Math.max(...samples) > 30,
      `The gap should separate actual frame content: ${samples}`,
    );
  }
  const download = await fetch(`${base}/api/jobs/${job.id}/image?download=1`);
  assert.equal(download.status, 200);
  assert.match(download.headers.get("content-disposition"), /contact-sheet/);
  if (!keep) await fetch(`${base}/api/jobs/${job.id}`, { method: "DELETE" });
  return { job, imageBytes };
}

async function waitForJob(id) {
  for (let attempt = 0; attempt < 100; attempt++) {
    const job = await (await fetch(`${base}/api/jobs/${id}`)).json();
    if (job.status === "done") return job;
    if (job.status === "error") throw new Error(job.error);
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error("Timed out waiting for image");
}

async function testEditing(initial) {
  const id = initial.job.id;
  const patch = (removedFrames) =>
    fetch(`${base}/api/jobs/${id}/frames`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ removedFrames }),
    });
  let response = await patch(initial.job.sampledFrames);
  assert.equal(response.status, 400);
  response = await patch([
    initial.job.sampledFrames[0],
    initial.job.sampledFrames[4],
  ]);
  assert.equal(response.status, 202);
  let job = await waitForJob(id);
  assert.equal(job.revision, 2);
  assert.deepEqual(job.removedFrames, [1, 6]);
  let edited = Buffer.from(
    await (await fetch(`${base}/api/jobs/${id}/image`)).arrayBuffer(),
  );
  assert.notDeepEqual(edited, initial.imageBytes);
  response = await patch([1, 6, 11]);
  assert.equal(response.status, 202);
  job = await waitForJob(id);
  assert.equal(job.revision, 3);
  assert.deepEqual(job.removedFrames, [1, 6, 11]);
  response = await patch([]);
  assert.equal(response.status, 202);
  job = await waitForJob(id);
  assert.equal(job.revision, 4);
  assert.deepEqual(job.removedFrames, []);
  const restored = Buffer.from(
    await (await fetch(`${base}/api/jobs/${id}/image`)).arrayBuffer(),
  );
  assert.deepEqual(restored, initial.imageBytes);
}

async function testSpacing(initial) {
  const id = initial.job.id;
  const remove = (removedFrames) =>
    fetch(`${base}/api/jobs/${id}/frames`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ removedFrames }),
    });
  async function update(spacingPercent) {
    const response = await fetch(`${base}/api/jobs/${id}/spacing`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ spacingPercent }),
    });
    return response;
  }
  assert.equal((await remove([1])).status, 202);
  await waitForJob(id);
  const edited = Buffer.from(
    await (await fetch(`${base}/api/jobs/${id}/image`)).arrayBuffer(),
  );
  assert.equal((await update(5)).status, 400);
  for (const value of [0, 4, 0.75]) {
    assert.equal((await update(value)).status, 202);
    const job = await waitForJob(id);
    assert.equal(job.spacingPercent, value);
    assert.deepEqual(job.removedFrames, [1]);
    const bytes = Buffer.from(
      await (await fetch(`${base}/api/jobs/${id}/image`)).arrayBuffer(),
    );
    if (value === 0.75) assert.deepEqual(bytes, edited);
    else assert.notDeepEqual(bytes, edited);
  }
  assert.equal((await remove([])).status, 202);
  await waitForJob(id);
  const restored = Buffer.from(
    await (await fetch(`${base}/api/jobs/${id}/image`)).arrayBuffer(),
  );
  assert.deepEqual(restored, initial.imageBytes);
  await fetch(`${base}/api/jobs/${id}`, { method: "DELETE" });
}

async function testInvalidSize(source) {
  const data = new FormData();
  data.append(
    "video",
    new Blob([await fs.readFile(source)], { type: "video/mp4" }),
    "invalid.mp4",
  );
  data.append("rows", "3");
  data.append("columns", "3");
  data.append("outputSize", "1000");
  data.append("spacingPercent", "0.75");
  data.append("format", "jpg");
  const response = await fetch(`${base}/api/jobs`, {
    method: "POST",
    body: data,
  });
  assert.equal(response.status, 400);
}

try {
  const eleven = await makeVideo("eleven.mp4", 11);
  const five = await makeVideo("five.mp4", 5);
  const hundred = await makeVideo("hundred.mp4", 100);
  const portrait = await makeVideo("portrait.mp4", 11, "54x96");
  await waitForServer();
  const initial = await testGrid(
    eleven,
    3,
    3,
    "jpg",
    1024,
    11,
    [1, 2, 4, 5, 6, 7, 9, 10, 11],
    true,
  );
  await testEditing(initial);
  await testSpacing(initial);
  await testGrid(eleven, 2, 2, "png", 2048, 11, [1, 4, 8, 11]);
  await testGrid(
    portrait,
    3,
    3,
    "png",
    1024,
    11,
    [1, 2, 4, 5, 6, 7, 9, 10, 11],
    false,
    0.75,
    9 / 16,
  );
  await testGrid(eleven, 3, 2, "jpg", 4096, 11, [1, 3, 5, 7, 9, 11]);
  await testGrid(five, 3, 3, "jpg", 512, 5, [1, 2, 3, 4, 5]);
  await testGrid(
    hundred,
    10,
    10,
    "jpg",
    512,
    100,
    Array.from({ length: 100 }, (_, index) => index + 1),
    false,
    4,
  );
  await testGrid(
    hundred,
    3,
    3,
    "jpg",
    1024,
    100,
    [1, 13, 26, 38, 51, 63, 75, 88, 100],
  );
  await testInvalidSize(five);
  console.log(
    "Smoke test passed: fixed-size image, adjustable spacing, frame deletion and restore.",
  );
} finally {
  server.kill();
  await fs.rm(temp, { recursive: true, force: true });
}
