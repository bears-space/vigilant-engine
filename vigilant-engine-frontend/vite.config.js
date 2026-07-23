import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import { viteSingleFile } from "vite-plugin-singlefile";
import { execSync } from "node:child_process";
import path from "node:path";

function getGitHash() {
  const envHash =
    process.env.VITE_GIT_HASH ??
    process.env.GIT_HASH ??
    process.env.GITHUB_SHA ??
    process.env.COMMIT_SHA;

  if (envHash?.trim()) {
    return envHash.trim();
  }

  try {
    return execSync("git rev-parse HEAD", {
      cwd: __dirname,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return "unknown";
  }
}

export default defineConfig(({ mode }) => {
  const isVigilant = mode === "vigilant";
  const isRecovery = mode === "recovery";
  const gitHash = getGitHash();

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
    define: {
      __VIGILANT_ENGINE_GIT_HASH__: JSON.stringify(gitHash),
    },
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
