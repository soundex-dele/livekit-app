import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
export default defineConfig({
  base: '/apps/admin/', plugins: [react()],
  server: {port: 3040, strictPort: true, proxy: {'/api': {target: process.env.AV_PROXY_TARGET || 'http://127.0.0.1:8080', changeOrigin: false}}},
  build: {chunkSizeWarningLimit: 1200, rollupOptions: {output: {manualChunks: id => id.includes('/node_modules/') ? (id.includes('/@platform/') ? 'foundation' : 'vendor') : undefined}}},
});
