const $ = (id) => document.getElementById(id);
const fileInput = $("file-input");
const dropzone = $("dropzone");
const preview = $("video-preview");
let file = null;
let previewUrl = null;
let job = null;
let pollTimer = null;
let format = "jpg";
let draftRemoved = new Set();

function bytes(size) {
  if (size >= 1024 ** 3) return `${(size / 1024 ** 3).toFixed(2)} GB`;
  if (size >= 1024 ** 2) return `${(size / 1024 ** 2).toFixed(1)} MB`;
  return `${Math.round(size / 1024)} KB`;
}

function time(seconds) {
  if (!Number.isFinite(seconds)) return "-";
  const minutes = Math.floor(seconds / 60);
  return `${String(minutes).padStart(2, "0")}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;
}

function showError(message) {
  $("error-message").textContent = message;
  $("error-message").hidden = false;
}

function clearError() {
  $("error-message").hidden = true;
}

function setFile(nextFile) {
  if (!nextFile) return;
  if (nextFile.size > 2 * 1024 ** 3) return showError("视频不能超过 2 GB。");
  clearError();
  file = nextFile;
  if (previewUrl) URL.revokeObjectURL(previewUrl);
  previewUrl = URL.createObjectURL(file);
  preview.src = previewUrl;
  preview.hidden = false;
  $("empty-video").hidden = true;
  dropzone.classList.add("has-video");
  $("video-details").hidden = false;
  $("file-name").textContent = file.name;
  $("file-size").textContent = bytes(file.size);
  $("video-resolution").textContent = "-";
  $("video-duration").textContent = "-";
  $("generate-button").disabled = false;
  $("settings-hint").textContent = "视频将在本机处理";
}

function clearFile() {
  file = null;
  fileInput.value = "";
  preview.pause();
  preview.removeAttribute("src");
  preview.load();
  if (previewUrl) URL.revokeObjectURL(previewUrl);
  previewUrl = null;
  preview.hidden = true;
  $("empty-video").hidden = false;
  dropzone.classList.remove("has-video");
  $("video-details").hidden = true;
  $("file-size").textContent = "";
  $("generate-button").disabled = true;
  $("settings-hint").textContent = "选择视频后即可开始";
}

function updateGridLabel() {
  const rows = Number($("rows").value);
  const columns = Number($("columns").value);
  $("frames-per-sheet").textContent =
    rows >= 1 && columns >= 1 && rows <= 10 && columns <= 10
      ? `选取 ${rows * columns} 帧`
      : "请输入 1 到 10";
}

function updateSpacingLabel() {
  const percent = Number($("spacing-percent").value);
  const pixels = Math.round((Number($("output-size").value) * percent) / 100);
  $("spacing-value").textContent = `${percent}% · ${pixels} px`;
  $("update-spacing").hidden =
    !job || job.status !== "done" || job.spacingPercent === percent;
}

function imageUrl(download = false) {
  return `/api/jobs/${job.id}/image?version=${job.revision}${download ? "&download=1" : ""}`;
}

function openImage() {
  draftRemoved = new Set(job.removedFrames);
  $("dialog-title").textContent = "拼图预览";
  $("dialog-image").src = imageUrl();
  $("dialog-download").href = imageUrl(true);
  renderFrameEditor();
  $("image-dialog").showModal();
}

function renderFrameEditor() {
  const grid = $("frame-grid");
  grid.replaceChildren();
  const visibleFrames = job.sampledFrames.filter(
    (frame) => !job.removedFrames.includes(frame),
  );
  for (const [index, frame] of visibleFrames.entries()) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "frame-cell";
    button.classList.toggle("marked", draftRemoved.has(frame));
    button.setAttribute(
      "aria-label",
      `第 ${frame} 帧${draftRemoved.has(frame) ? "，已标记删除" : "，点击标记删除"}`,
    );
    button.title = `第 ${frame} 帧`;
    const { gap, cellWidth, cellHeight, offsetX, offsetY } = job.layout;
    const column = index % job.columns;
    const row = Math.floor(index / job.columns);
    button.style.left = `${((offsetX + gap + column * (cellWidth + gap)) / job.outputSize) * 100}%`;
    button.style.top = `${((offsetY + gap + row * (cellHeight + gap)) / job.outputSize) * 100}%`;
    button.style.width = `${(cellWidth / job.outputSize) * 100}%`;
    button.style.height = `${(cellHeight / job.outputSize) * 100}%`;
    const mark = document.createElement("span");
    mark.textContent = "×";
    button.append(mark);
    button.addEventListener("click", () => {
      if (draftRemoved.has(frame)) draftRemoved.delete(frame);
      else draftRemoved.add(frame);
      renderFrameEditor();
    });
    grid.append(button);
  }
  const remaining = job.sampledFrames.length - draftRemoved.size;
  $("edit-count").textContent =
    `保留 ${remaining} 帧 · 删除 ${draftRemoved.size} 帧`;
  $("apply-frames").disabled =
    remaining < 1 || draftRemoved.size === job.removedFrames.length;
  $("restore-frames").disabled = draftRemoved.size === 0;
}

async function applyFrames() {
  const response = await fetch(`/api/jobs/${job.id}/frames`, {
    method: "PATCH",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ removedFrames: [...draftRemoved] }),
  });
  const result = await response.json();
  if (!response.ok) return showError(result.error || "更新失败，请重试。");
  clearError();
  $("image-dialog").close();
  startProcessing(result);
}

function startProcessing(result) {
  $("result-section").hidden = true;
  job = result;
  renderJob();
  updateSpacingLabel();
  pollTimer = setInterval(poll, 750);
  void poll();
}

async function applySpacing() {
  $("update-spacing").disabled = true;
  try {
    const response = await fetch(`/api/jobs/${job.id}/spacing`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        spacingPercent: Number($("spacing-percent").value),
      }),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "更新间距失败，请重试。");
    clearError();
    startProcessing(result);
  } catch (error) {
    showError(error.message);
  } finally {
    $("update-spacing").disabled = false;
  }
}

function renderJob() {
  if (!job) return;
  const working = ["queued", "probing", "processing"].includes(job.status);
  $("progress-section").hidden = !working;
  $("generate-button").disabled = working || !file;
  if (working) {
    const labels = {
      queued: "准备处理",
      probing: "正在读取视频",
      processing: "正在选取画面",
    };
    $("progress-title").textContent = labels[job.status];
    const target = job.sampledFrames.length - job.removedFrames.length;
    $("progress-count").textContent = target
      ? `${job.completedFrames} / ${target} 帧`
      : "准备中";
    $("progress-detail").textContent = job.frameCount
      ? `共 ${job.frameCount.toLocaleString()} 帧 · ${job.columns} × ${job.rows} 网格`
      : "正在统计视频帧";
    $("progress-track").classList.toggle("indeterminate", !target);
    $("progress-bar").style.width = target
      ? `${Math.min(99, (job.completedFrames / target) * 100)}%`
      : "30%";
  }
  if (job.status === "done") {
    $("result-section").hidden = false;
    const count = job.sampledFrames.length - job.removedFrames.length;
    $("result-summary").textContent =
      `${job.frameCount.toLocaleString()} 帧视频 · ${job.removedFrames.length ? "保留" : count === job.frameCount ? "保留全部" : "均匀选取"} ${count} 帧 · ${job.columns} × ${job.rows} · ${job.outputSize} × ${job.outputSize}`;
    updateSpacingLabel();
    $("result-image").src = imageUrl();
    $("result-image").alt = `${job.columns} 列 ${job.rows} 行的视频拼图`;
    $("download-image").href = imageUrl(true);
    $("result-section").scrollIntoView({ behavior: "smooth", block: "start" });
  }
  if (job.status === "error") showError(job.error || "处理失败，请重试。");
}

async function poll() {
  if (!job) return;
  try {
    const response = await fetch(`/api/jobs/${job.id}`);
    if (!response.ok) throw new Error("结果已过期，请重新处理。");
    job = await response.json();
    renderJob();
    if (["done", "error", "cancelled"].includes(job.status)) {
      clearInterval(pollTimer);
      pollTimer = null;
      if (job.status !== "done") localStorage.removeItem("frameGridJob");
    }
  } catch (error) {
    clearInterval(pollTimer);
    pollTimer = null;
    $("progress-section").hidden = true;
    showError(error.message);
    localStorage.removeItem("frameGridJob");
  }
}

async function generate() {
  if (!file) return;
  clearError();
  const rows = Number($("rows").value);
  const columns = Number($("columns").value);
  if (
    ![rows, columns].every(
      (value) => Number.isInteger(value) && value >= 1 && value <= 10,
    )
  ) {
    showError("行数和列数应在 1 到 10 之间。");
    return;
  }
  $("generate-button").disabled = true;
  $("progress-section").hidden = false;
  $("progress-track").classList.add("indeterminate");
  $("progress-title").textContent = "正在上传视频";
  $("progress-count").textContent = "准备中";
  $("progress-detail").textContent = file.name;
  $("progress-bar").style.width = "30%";
  $("cancel-button").hidden = true;
  const data = new FormData();
  data.append("video", file);
  data.append("rows", rows);
  data.append("columns", columns);
  data.append("outputSize", $("output-size").value);
  data.append("spacingPercent", $("spacing-percent").value);
  data.append("format", format);
  try {
    const response = await fetch("/api/jobs", { method: "POST", body: data });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "上传失败，请重试。");
    job = result;
    localStorage.setItem("frameGridJob", job.id);
    $("result-section").hidden = true;
    $("cancel-button").hidden = false;
    renderJob();
    pollTimer = setInterval(poll, 750);
    await poll();
  } catch (error) {
    $("progress-section").hidden = true;
    $("generate-button").disabled = false;
    $("cancel-button").hidden = false;
    showError(error.message);
  }
}

async function cancel() {
  if (!job) return;
  clearInterval(pollTimer);
  pollTimer = null;
  await fetch(`/api/jobs/${job.id}`, { method: "DELETE" });
  job = null;
  localStorage.removeItem("frameGridJob");
  $("progress-section").hidden = true;
  $("generate-button").disabled = !file;
}

function chooseFile(event) {
  event?.stopPropagation();
  fileInput.click();
}

$("choose-button").addEventListener("click", chooseFile);
$("replace-button").addEventListener("click", chooseFile);
dropzone.addEventListener("click", () => {
  if (!file) chooseFile();
});
dropzone.addEventListener("keydown", (event) => {
  if (!file && ["Enter", " "].includes(event.key)) {
    event.preventDefault();
    chooseFile();
  }
});
fileInput.addEventListener("change", () => setFile(fileInput.files[0]));
preview.addEventListener("loadedmetadata", () => {
  $("video-resolution").textContent =
    `${preview.videoWidth} × ${preview.videoHeight}`;
  $("video-duration").textContent = time(preview.duration);
});
for (const name of ["dragenter", "dragover"])
  dropzone.addEventListener(name, (event) => {
    event.preventDefault();
    dropzone.classList.add("dragging");
  });
for (const name of ["dragleave", "drop"])
  dropzone.addEventListener(name, (event) => {
    event.preventDefault();
    dropzone.classList.remove("dragging");
  });
dropzone.addEventListener("drop", (event) =>
  setFile(event.dataTransfer.files[0]),
);
for (const id of ["rows", "columns"])
  $(id).addEventListener("input", updateGridLabel);
$("spacing-percent").addEventListener("input", updateSpacingLabel);
$("output-size").addEventListener("change", updateSpacingLabel);
document.querySelectorAll("[data-format]").forEach((button) =>
  button.addEventListener("click", () => {
    format = button.dataset.format;
    document.querySelectorAll("[data-format]").forEach((item) => {
      const active = item === button;
      item.classList.toggle("active", active);
      item.setAttribute("aria-pressed", String(active));
    });
  }),
);
$("generate-button").addEventListener("click", generate);
$("cancel-button").addEventListener("click", cancel);
$("new-button").addEventListener("click", async () => {
  if (job) await fetch(`/api/jobs/${job.id}`, { method: "DELETE" });
  job = null;
  localStorage.removeItem("frameGridJob");
  $("result-section").hidden = true;
  clearFile();
  clearError();
  window.scrollTo({ top: 0, behavior: "smooth" });
});
$("result-image").addEventListener("click", openImage);
$("edit-frames").addEventListener("click", openImage);
$("apply-frames").addEventListener("click", applyFrames);
$("update-spacing").addEventListener("click", applySpacing);
$("restore-frames").addEventListener("click", () => {
  draftRemoved.clear();
  renderFrameEditor();
});
$("dialog-close").addEventListener("click", () => $("image-dialog").close());
$("image-dialog").addEventListener("click", (event) => {
  if (event.target === $("image-dialog")) $("image-dialog").close();
});

window.lucide?.createIcons();
updateSpacingLabel();
const savedJob = localStorage.getItem("frameGridJob");
if (savedJob) {
  job = { id: savedJob };
  poll().then(() => {
    if (job?.status === "done" && !file) {
      $("spacing-percent").value = job.spacingPercent;
      $("output-size").value = job.outputSize;
      updateSpacingLabel();
    }
    if (job && !["done", "error"].includes(job.status))
      pollTimer = setInterval(poll, 750);
  });
}
