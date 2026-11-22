import multer from "multer";
import sharp from "sharp";
import yazl from "yazl";
import fs from "node:fs/promises";
import { createWriteStream } from "node:fs";
import { pipeline } from "node:stream/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";

const limitInputPixels = 64_000_000;

export function registerImageSlices(app, tempRoot) {
  const jobs = new Map();
  const upload = multer({
    dest: tempRoot,
    limits: { fileSize: 50 * 1024 * 1024 },
  });
  let processing = false;

  app.post(
    "/api/slices",
    (request, response, next) => {
      upload.single("image")(request, response, (error) => {
        if (error)
          return response
            .status(400)
            .json({ error: "请选择不超过 50 MB 的图片。" });
        next();
      });
    },
    async (request, response) => {
      const rows = Number(request.body.rows);
      const columns = Number(request.body.columns);
      if (
        !request.file ||
        ![rows, columns].every((n) => Number.isInteger(n) && n >= 1 && n <= 10)
      ) {
        if (request.file) await fs.rm(request.file.path, { force: true });
        return response
          .status(400)
          .json({ error: "请提供图片，行数和列数应在 1 到 10 之间。" });
      }
      if (processing) {
        await fs.rm(request.file.path, { force: true });
        return response
          .status(409)
          .json({ error: "正在切分另一张图片，请稍后重试。" });
      }
      processing = true;
      const id = randomUUID();
      const dir = path.join(tempRoot, `slices-${id}`);
      try {
        // Normalize EXIF orientation before measuring and cropping. Only the first
        // frame/page is used for animated or multi-page image inputs.
        const { data, info } = await sharp(request.file.path, {
          limitInputPixels,
        })
          .autoOrient()
          .png()
          .toBuffer({ resolveWithObject: true });
        if (info.width < columns || info.height < rows) {
          return response
            .status(400)
            .json({ error: "图片像素尺寸小于所选行列数，请减少行列数。" });
        }
        await fs.mkdir(dir);
        const tiles = [];
        for (let row = 0; row < rows; row++) {
          for (let column = 0; column < columns; column++) {
            const left = Math.floor((column * info.width) / columns);
            const top = Math.floor((row * info.height) / rows);
            const width =
              Math.floor(((column + 1) * info.width) / columns) - left;
            const height = Math.floor(((row + 1) * info.height) / rows) - top;
            const number = tiles.length + 1;
            const name = `tile_${String(number).padStart(2, "0")}_r${row + 1}_c${column + 1}.png`;
            await sharp(data, { limitInputPixels })
              .extract({ left, top, width, height })
              .png()
              .toFile(path.join(dir, name));
            tiles.push({
              number,
              row: row + 1,
              column: column + 1,
              left,
              top,
              width,
              height,
              name,
            });
          }
        }
        const zip = new yazl.ZipFile();
        const zipped = pipeline(
          zip.outputStream,
          createWriteStream(path.join(dir, "slices.zip")),
        );
        for (const tile of tiles)
          zip.addFile(path.join(dir, tile.name), tile.name);
        zip.end();
        await zipped;
        const job = {
          id,
          rows,
          columns,
          width: info.width,
          height: info.height,
          tiles,
        };
        jobs.set(id, { job, dir, createdAt: Date.now() });
        response.status(201).json(job);
      } catch (error) {
        await fs.rm(dir, { recursive: true, force: true });
        console.error("Image slicing failed:", error.message);
        response
          .status(400)
          .json({
            error:
              "图片无法读取或切分，请使用 PNG、JPG、WebP 等图片重试（最多 6400 万像素）。",
          });
      } finally {
        processing = false;
        await fs.rm(request.file.path, { force: true });
      }
    },
  );

  app.get("/api/slices/:id/tiles/:number", (request, response) => {
    const entry = jobs.get(request.params.id);
    const tile = entry?.job.tiles.find(
      (item) => item.number === Number(request.params.number),
    );
    if (!tile) return response.status(404).end();
    const filename = path.join(entry.dir, tile.name);
    if (request.query.download === "1")
      return response.download(filename, tile.name);
    response.sendFile(filename);
  });

  app.get("/api/slices/:id/download", (request, response) => {
    const entry = jobs.get(request.params.id);
    if (!entry) return response.status(404).end();
    response.download(
      path.join(entry.dir, "slices.zip"),
      `image-slices-${entry.job.columns}x${entry.job.rows}.zip`,
    );
  });

  setInterval(
    async () => {
      for (const [id, entry] of jobs) {
        if (Date.now() - entry.createdAt < 60 * 60 * 1000) continue;
        jobs.delete(id);
        await fs.rm(entry.dir, { recursive: true, force: true });
      }
    },
    10 * 60 * 1000,
  ).unref();
}
