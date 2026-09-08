import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'
import VueI18nPlugin from '@intlify/unplugin-vue-i18n/vite'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

export default defineConfig({
  plugins: [
    vue(),
    tailwindcss(),
    // #159: AOT-precompile the UI catalogs (plain default-export TS modules —
    // the error catalog under services/messages is a plain lookup and stays
    // out of scope). Paths are explicit so application code is never matched;
    // in production builds the plugin also swaps vue-i18n to its runtime-only
    // bundle, dropping the message compiler. Dev/vitest keep the full build.
    VueI18nPlugin({
      include: [
        path.resolve(__dirname, 'src/i18n/en.ts'),
        path.resolve(__dirname, 'src/i18n/zh-CN.ts'),
      ],
    }),
  ],
  base: '/admin/',
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src')
    },
    extensions: ['.ts', '.js', '.vue', '.json']
  },
  server: {
    port: 5174,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/oauth2': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/health': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/.well-known': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
    },
  },
})
