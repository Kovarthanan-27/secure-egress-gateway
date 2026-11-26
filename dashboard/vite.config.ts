import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// In development the dashboard runs on :5173 and forwards /api to the Spring Boot service.
// In Docker, nginx does the same job (see nginx.conf), so the browser always talks to ONE origin
// and we never need CORS.
export default defineConfig({
  plugins: [react()],
  server: { port: 5173, proxy: { '/api': 'http://localhost:8081' } },
});
