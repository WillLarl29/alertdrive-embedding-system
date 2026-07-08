# El frontend ahora corre en su propio contenedor (frontend/Dockerfile, nginx) que
# le hace de proxy a este backend, asi que aqui ya no hace falta copiar frontend/.
# express.static sigue apuntando a ../../frontend en server.js (para `npm start`
# local fuera de Docker); dentro de este contenedor esa carpeta simplemente no
# existe y express.static no hace nada con ella.
FROM node:22-alpine

WORKDIR /app/backend

COPY package.json pnpm-lock.yaml ./
RUN corepack enable && pnpm install --prod --frozen-lockfile

COPY src ./src

EXPOSE 3000
CMD ["node", "src/server.js"]
