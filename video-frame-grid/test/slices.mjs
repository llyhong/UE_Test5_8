import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const temp = await fs.mkdtemp(path.join(os.tmpdir(), "slices-test-"));
const port = 45000 + Math.floor(Math.random() * 8000);
const base = `http://127.0.0.1:${port}`;
const child = spawn(process.execPath, ["server.mjs"], {
  cwd: root,
  env: { ...process.env, PORT: String(port) },
  windowsHide: true,
  stdio: "ignore",
});
async function post(image, rows = 3, columns = 2) {
  const data = new FormData();
  data.append("image", new Blob([image]), "sample.png");
  data.append("rows", String(rows));
  data.append("columns", String(columns));
  return fetch(`${base}/api/slices`, { method: "POST", body: data });
}
try {
  for (let attempt = 0; ; attempt++) {
    try {
      if ((await fetch(base)).ok) break;
    } catch {}
    if (attempt > 80) throw new Error("Server failed to start");
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  const width = 101,
    height = 77,
    channels = 4;
  const raw = Buffer.alloc(width * height * channels);
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++) {
      const index = (y * width + x) * channels;
      raw[index] = x;
      raw[index + 1] = y;
      raw[index + 2] = (x + y) % 256;
      raw[index + 3] = x % 3 ? 255 : 70;
    }
  const source = await sharp(raw, { raw: { width, height, channels } })
    .png()
    .toBuffer();
  const response = await post(source);
  assert.equal(response.status, 201);
  const job = await response.json();
  assert.equal(job.tiles.length, 6);
  assert.deepEqual(
    job.tiles.map(({ row, column }) => [row, column]),
    [
      [1, 1],
      [1, 2],
      [2, 1],
      [2, 2],
      [3, 1],
      [3, 2],
    ],
  );
  const reconstructed = Buffer.alloc(raw.length);
  for (const tile of job.tiles) {
    const download = await fetch(
      `${base}/api/slices/${job.id}/tiles/${tile.number}?download=1`,
    );
    assert.equal(download.status, 200);
    assert.match(download.headers.get("content-disposition"), /attachment/);
    const { data, info } = await sharp(
      Buffer.from(await download.arrayBuffer()),
    )
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });
    assert.equal(info.width, tile.width);
    assert.equal(info.height, tile.height);
    for (let row = 0; row < tile.height; row++) {
      data.copy(
        reconstructed,
        ((tile.top + row) * width + tile.left) * channels,
        row * tile.width * channels,
        (row + 1) * tile.width * channels,
      );
    }
  }
  assert.deepEqual(
    reconstructed,
    raw,
    "All source pixels and alpha must survive slicing",
  );
  const zipped = Buffer.from(
    await (await fetch(`${base}/api/slices/${job.id}/download`)).arrayBuffer(),
  );
  const archive = path.join(temp, "slices.zip");
  await fs.writeFile(archive, zipped);
  // Inspect ZIP's central directory and count image entries.
  const entries = [];
  for (let offset = 0; offset + 46 <= zipped.length; offset++) {
    if (zipped.readUInt32LE(offset) !== 0x02014b50) continue;
    const length = zipped.readUInt16LE(offset + 28);
    entries.push(zipped.subarray(offset + 46, offset + 46 + length).toString());
  }
  assert.deepEqual(
    entries,
    job.tiles.map((tile) => tile.name),
  );
  assert.equal((await post(source, 0, 2)).status, 400);
  assert.equal((await post(Buffer.from("not an image"))).status, 400);
  const tiny = await sharp({
    create: { width: 1, height: 1, channels: 4, background: "red" },
  })
    .png()
    .toBuffer();
  assert.equal((await post(tiny)).status, 400);
  console.log(
    "Image slicing passed: 2 columns x 3 rows, 6 PNGs, exact pixel reconstruction, alpha, ZIP and invalid input.",
  );
} finally {
  child.kill();
  await fs.rm(temp, { recursive: true, force: true });
}
