import { defineConfig } from 'vite';
export default defineConfig({ base: "/apps/remote-control/", server: { proxy: { '/api': 'http://127.0.0.1:8080' } } });
