import {defineConfig} from 'vite';
export default defineConfig({base:'/apps/live-studio/',server:{proxy:{'/api':{target:'http://127.0.0.1:8080',ws:true}}}});
