import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  // Built assets land in web/, which is what nginx serves and what the
  // Docker image copies. Keeping one output path means the deployment does
  // not need to know whether the UI was hand-written or built.
  build: {
    outDir: "../web",
    emptyOutDir: true,
    sourcemap: false,
  },
  server: {
    port: 5173,
    // `npm run dev` gives hot reload while talking to the real API, so the
    // frontend can be developed without rebuilding the C++ container.
    proxy: {
      "/api": { target: "http://localhost:8081", changeOrigin: true },
      "/health": { target: "http://localhost:8081", changeOrigin: true },
    },
  },
});
