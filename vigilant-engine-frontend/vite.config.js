import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import { viteSingleFile } from "vite-plugin-singlefile";
import path from "node:path";

export default defineConfig(({ mode }) => {
  const isVigilant = mode === "vigilant";
  const isRecovery = mode === "recovery";

  // Decide which HTML entry to open in dev server
  const devEntry = isVigilant
    ? "/vigilant.html"
    : isRecovery
      ? "/index.html"
      : "/";

  // ✅ Adjust these relative paths to your repo layout
  const outDir = isVigilant
    ? path.resolve(__dirname, "../build/static/vigilant")
    : isRecovery
      ? path.resolve(__dirname, "../build/static/recovery")
      : path.resolve(__dirname, "dist");

  const input = isVigilant
    ? { vigilant: path.resolve(__dirname, "vigilant.html") }
    : isRecovery
      ? { recovery: path.resolve(__dirname, "index.html") }
      : {
          vigilant: path.resolve(__dirname, "vigilant.html"),
          recovery: path.resolve(__dirname, "index.html"),
        };

  return {
    // MPA avoids history-fallback forcing /index.html and lets us open either page directly
    appType: "mpa",
    plugins: [vue(), viteSingleFile()],
    server: {
      open: devEntry,
    },
    build: {
      outDir,
      emptyOutDir: true,
      cssCodeSplit: false,
      assetsInlineLimit: 100_000_000,
      rollupOptions: {
        input,
        output: {
          entryFileNames: "_app.js",
          chunkFileNames: "_chunk.js",
          assetFileNames: "_asset.[ext]",
        },
      },
      minify: "esbuild",
    },
  };
});
