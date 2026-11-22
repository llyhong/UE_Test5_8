(() => {
  const element = (id) => document.getElementById(id);
  const input = element("slice-file");
  const preview = element("slice-preview");
  let source = null;
  let objectUrl = null;
  let busy = false;

  function error(message) {
    element("slice-error").textContent = message;
    element("slice-error").hidden = !message;
  }

  function layout() {
    const rows = Number(element("slice-rows").value);
    const columns = Number(element("slice-columns").value);
    return [rows, columns].every(
      (n) => Number.isInteger(n) && n >= 1 && n <= 10,
    )
      ? { rows, columns }
      : null;
  }

  function updateGrid() {
    const grid = element("slice-preview-grid");
    grid.replaceChildren();
    const config = layout();
    element("slice-count").textContent = config
      ? `共 ${config.rows * config.columns} 张图片`
      : "请输入 1 到 10";
    element("slice-submit").disabled =
      busy || !source || !preview.naturalWidth || !config;
    if (!config) return;
    grid.style.gridTemplateColumns = `repeat(${config.columns}, minmax(0, 1fr))`;
    grid.style.gridTemplateRows = `repeat(${config.rows}, minmax(0, 1fr))`;
    for (let index = 1; index <= config.rows * config.columns; index++) {
      const cell = document.createElement("span");
      const label = document.createElement("b");
      label.textContent = index;
      cell.append(label);
      grid.append(cell);
    }
  }

  function setSource(file) {
    if (!file || busy) return;
    if (file.size > 50 * 1024 * 1024)
      return error("请选择不超过 50 MB 的图片。");
    error("");
    source = file;
    if (objectUrl) URL.revokeObjectURL(objectUrl);
    objectUrl = URL.createObjectURL(file);
    preview.src = objectUrl;
    element("slice-submit").disabled = true;
    element("slice-empty").hidden = true;
    element("slice-preview-wrap").hidden = false;
    element("slice-replace").hidden = false;
    element("slice-file-name").textContent = file.name;
    element("slice-file-meta").textContent = "读取中";
    element("slice-status").textContent = "准备切分图片";
  }

  function renderResults(job) {
    const gallery = element("slice-gallery");
    gallery.replaceChildren();
    element("slice-summary").textContent =
      `${job.width} × ${job.height} 原图 · ${job.columns} 列 × ${job.rows} 行 · ${job.tiles.length} 张 PNG`;
    element("slice-download-all").href = `/api/slices/${job.id}/download`;
    for (const tile of job.tiles) {
      const card = document.createElement("article");
      card.className = "slice-card";
      const url = `/api/slices/${job.id}/tiles/${tile.number}`;
      const link = document.createElement("a");
      link.href = `${url}?download=1`;
      link.title = `下载切片 ${tile.number}`;
      const image = document.createElement("img");
      image.src = url;
      image.alt = `第 ${tile.row} 行、第 ${tile.column} 列`;
      image.loading = "lazy";
      link.append(image);
      const footer = document.createElement("div");
      footer.className = "slice-card-footer";
      const label = document.createElement("span");
      label.textContent = `${String(tile.number).padStart(2, "0")} · ${tile.width} × ${tile.height}`;
      const download = document.createElement("a");
      download.href = `${url}?download=1`;
      download.textContent = "下载";
      download.setAttribute("aria-label", `下载切片 ${tile.number}`);
      footer.append(label, download);
      card.append(link, footer);
      gallery.append(card);
    }
    element("slice-results").hidden = false;
    element("slice-results").scrollIntoView({
      behavior: "smooth",
      block: "start",
    });
  }

  function choose(event) {
    event?.stopPropagation();
    if (!busy) input.click();
  }
  element("slice-choose").addEventListener("click", choose);
  element("slice-replace").addEventListener("click", choose);
  input.addEventListener("change", () => setSource(input.files[0]));
  preview.addEventListener("load", () => {
    element("slice-file-meta").textContent =
      `${preview.naturalWidth} × ${preview.naturalHeight}`;
    updateGrid();
  });
  preview.addEventListener("error", () => {
    source = null;
    element("slice-submit").disabled = true;
    error("图片无法预览，请换一张 PNG、JPG 或 WebP 图片。");
  });
  for (const id of ["slice-rows", "slice-columns"])
    element(id).addEventListener("input", updateGrid);
  const zone = element("slice-dropzone");
  zone.addEventListener("click", choose);
  zone.addEventListener("keydown", (event) => {
    if (event.target === zone && ["Enter", " "].includes(event.key)) {
      event.preventDefault();
      choose();
    }
  });
  for (const name of ["dragenter", "dragover"])
    zone.addEventListener(name, (event) => {
      event.preventDefault();
      zone.classList.add("dragging");
    });
  for (const name of ["dragleave", "drop"])
    zone.addEventListener(name, (event) => {
      event.preventDefault();
      zone.classList.remove("dragging");
    });
  zone.addEventListener("drop", (event) =>
    setSource(event.dataTransfer.files[0]),
  );

  element("slice-submit").addEventListener("click", async () => {
    const config = layout();
    if (!source || !config || busy) return;
    error("");
    busy = true;
    updateGrid();
    for (const id of ["slice-rows", "slice-columns"])
      element(id).disabled = true;
    element("slice-status").textContent = "正在切分并打包，请稍候…";
    const data = new FormData();
    data.append("image", source);
    data.append("rows", config.rows);
    data.append("columns", config.columns);
    try {
      const response = await fetch("/api/slices", {
        method: "POST",
        body: data,
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "切图失败，请重试。");
      renderResults(result);
      element("slice-status").textContent =
        `已完成，共 ${result.tiles.length} 张图片`;
    } catch (cause) {
      error(cause.message);
      element("slice-status").textContent = "切图未完成，可重新尝试";
    } finally {
      busy = false;
      for (const id of ["slice-rows", "slice-columns"])
        element(id).disabled = false;
      updateGrid();
    }
  });

  for (const mode of ["video", "slice"])
    element(`${mode}-tab`).addEventListener("click", () => {
      for (const name of ["video", "slice"]) {
        const active = mode === name;
        element(`${name}-tool`).hidden = !active;
        element(`${name}-tab`).classList.toggle("active", active);
        element(`${name}-tab`).setAttribute("aria-pressed", String(active));
      }
    });
  updateGrid();
})();
