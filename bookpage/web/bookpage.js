// BookPage on the web: the engine's EQP1 raw bitmap (12-byte header + RGBA8
// pixels) on a canvas, with the highlights drawn over it.
//
// The engine writes the file through WASI into the app's own file system, so
// the bitmap comes back with fs.readFile — the pixels never go through the
// Asuka runtime, the same as the desktop views.
//
// A press only remembers where it was: a drag makes a selection, and a lift
// without one is a tap that turns the page.

const { fs } = globalThis.asuka.native;

/// The header: "EQP1", then the size in device pixels.
function decode(bytes) {
  if (bytes.length < 12) return null;
  if (bytes[0] !== 0x45 || bytes[1] !== 0x51 || bytes[2] !== 0x50 || bytes[3] !== 0x31) return null; // EQP1
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const width = view.getUint32(4, true);
  const height = view.getUint32(8, true);
  const size = width * height * 4;
  if (!width || !height || bytes.length < 12 + size) return null;
  return { width, height, pixels: bytes.subarray(12, 12 + size) };
}

/// The color as CSS, from the app's #rrggbbaa an int carries.
function css(i) {
  const c = Number(i) >>> 0;
  return `rgba(${(c >>> 24) & 0xff}, ${(c >>> 16) & 0xff}, ${(c >>> 8) & 0xff}, ${(c & 0xff) / 255})`;
}

export function makeFontFiles() {
  return {
    /// The web has no font files to name: the reader registers the face it
    /// embeds, and the engine measures with that.
    path(family) {
      return "";
    },
  };
}

export function makeBookPage(events) {
  const canvas = document.createElement("canvas");
  canvas.style.cssText =
    "display:block;width:100%;height:100%;touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none";
  canvas.setAttribute("role", "img");

  let path = "";
  let bitmap = null;
  let marks = [];
  let scale = 1;
  let hasSelection = false;
  /// Where the pointer went down, and where it is now.
  let pressX = 0, pressY = 0, lastX = 0, lastY = 0;
  let down = false;
  /// Between starting a selection and lifting: motion extends it.
  let dragging = false;
  /// When the last selection was lifted: a click just after it is that
  /// selection's, not a page turn.
  let selectionEnded = 0;
  let holdTimer = 0;

  /// The highlights, over the page: the rectangles in the bitmap's own pixels.
  function paint() {
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (bitmap) {
      const image = ctx.createImageData(bitmap.width, bitmap.height);
      image.data.set(bitmap.pixels);
      ctx.putImageData(image, 0, 0);
    }
    const s = scale > 0 ? scale : 1;
    ctx.globalCompositeOperation = "source-over";
    for (const h of marks) {
      if (!h || h.w <= 0 || h.h <= 0) continue;
      const a = Number(h.color) & 0xff;
      if (a === 0) continue;
      ctx.fillStyle = css(h.color);
      ctx.fillRect(h.x * s, h.y * s, h.w * s, h.h * s);
    }
  }

  function setPath(next) {
    path = next || "";
    bitmap = null;
    if (path) {
      try {
        bitmap = decode(fs.readFile(path));
      } catch {
        // A page the engine is still writing: the next setPath brings it.
      }
    }
    if (bitmap) {
      canvas.width = bitmap.width;
      canvas.height = bitmap.height;
    }
    paint();
  }

  function setHighlights(value) {
    marks = value || [];
    paint();
  }

  function setSelecting(value) {
    hasSelection = !!value;
  }

  function setScale(value) {
    scale = value > 0 ? value : 1;
    paint();
  }

  function point(e) {
    const rect = canvas.getBoundingClientRect();
    return [e.clientX - rect.left, e.clientY - rect.top];
  }

  function pressed(e) {
    if (e.button !== undefined && e.button !== 0) return;
    [pressX, pressY] = point(e);
    lastX = pressX;
    lastY = pressY;
    down = true;
    canvas.setPointerCapture?.(e.pointerId);
    // A finger that stays put starts a selection; a mouse never waits.
    if (e.pointerType === "touch" && !dragging) {
      clearTimeout(holdTimer);
      holdTimer = setTimeout(() => {
        if (!down || dragging) return;
        dragging = true;
        events.hold(pressX, pressY);
      }, 500);
    }
  }

  function moved(e) {
    const [x, y] = point(e);
    lastX = x;
    lastY = y;
    if (dragging) {
      events.drag(x, y);
      return;
    }
    if (!down) return;
    const dx = x - pressX, dy = y - pressY;
    if (dx * dx + dy * dy < 25 && !hasSelection) return;
    clearTimeout(holdTimer);
    dragging = true;
    events.hold(pressX, pressY);
    events.drag(x, y);
  }

  function released(e) {
    clearTimeout(holdTimer);
    down = false;
    if (dragging) {
      dragging = false;
      selectionEnded = Date.now();
      events.release();
      return;
    }
    // The click that comes with a selection's lift is not a page turn.
    if (selectionEnded && Date.now() - selectionEnded < 800) return;
    events.press(lastX, lastY);
  }

  function cancelled() {
    clearTimeout(holdTimer);
    down = false;
    if (dragging) {
      dragging = false;
      selectionEnded = Date.now();
      events.release();
    }
  }

  canvas.addEventListener("pointerdown", pressed);
  canvas.addEventListener("pointermove", moved);
  canvas.addEventListener("pointerup", released);
  canvas.addEventListener("pointercancel", cancelled);

  return {
    el: canvas,
    setPath,
    setHighlights,
    setSelecting,
    setScale,
    destroy() {
      clearTimeout(holdTimer);
      canvas.removeEventListener("pointerdown", pressed);
      canvas.removeEventListener("pointermove", moved);
      canvas.removeEventListener("pointerup", released);
      canvas.removeEventListener("pointercancel", cancelled);
      marks = [];
      bitmap = null;
    },
  };
}
