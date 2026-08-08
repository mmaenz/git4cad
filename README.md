# git4cad

Git version control for CAD files. Push STEP and FreeCAD FCStd files with any standard git client, get automatic 3D preview generation and an in-browser GitHub-style viewer.

## Features

- **Standard git protocol** — works with any git client (`git clone`, `git push`, `git fetch`)
- **Automatic GLB conversion** — STEP and FCStd files are converted to GLB asynchronously on push (202 Accepted, no push delay)
- **FCStd assembly support** — full multi-body assemblies with quaternion placements
- **In-browser 3D viewer** — Three.js with OrbitControls, light/dark theme
- **Multi-user** — each user has their own namespace; token-based auth for API and git
- **Public/private repositories** — public repos are visible and cloneable by anyone; private repos are hidden from unauthenticated users and non-members
- **Collaborator access control** — owners grant per-user read-only or read+write access
- **Durable storage** — git repositories bind-mounted to the host; GLB blobs in SeaweedFS

## Quick start

```bash
git clone https://github.com/yourname/git4cad
cd git4cad
docker compose up -d
```

The stack is available at **http://localhost**.

1. Open http://localhost and register an account
2. Create a repository
3. Clone it, add STEP or FCStd files, push
4. Open the file in the browser to see the 3D viewer

## Git client usage

Authentication uses HTTP Basic with your username and API token as the password. The token is shown once immediately after registration.

```bash
# Clone a public repo — no credentials required
git clone http://localhost/alice/myproject.git

# Clone a private repo
git clone http://alice:TOKEN@localhost/alice/myproject.git

# Push (requires owner or collaborator with write access)
git push
```

The token never changes. Use any git credential helper to store it.

## API

All API endpoints are under `/api/v1/`. Protected endpoints require `Authorization: Bearer username:token`.

### Auth

| Method | Path | Auth | Body / Notes |
|--------|------|------|--------------|
| `POST` | `/api/v1/users` | — | `{"username": "alice", "password": "..."}` → `{"username": "alice", "token": "..."}` |
| `POST` | `/api/v1/auth/login` | Basic `username:password` | → `{"token": "...", "username": "alice"}` |

### Repositories

| Method | Path | Auth | Notes |
|--------|------|------|-------|
| `GET` | `/api/v1/repos` | Bearer (optional) | List visible repositories; unauthenticated callers see only public repos |
| `POST` | `/api/v1/repos` | Bearer | `{"name": "myrepo", "description": "...", "private": false}` → repo info |
| `GET` | `/api/v1/repos/:user/:repo` | Bearer (optional) | Repo metadata; returns 404 for private repos the caller cannot read |
| `PATCH` | `/api/v1/repos/:user/:repo` | Bearer (owner) | `{"private": true}` — toggle visibility |
| `DELETE` | `/api/v1/repos/:user/:repo` | Bearer (owner) | Delete repository |

### Collaborators

| Method | Path | Auth | Notes |
|--------|------|------|-------|
| `GET` | `/api/v1/repos/:user/:repo/members` | Bearer (owner) | List collaborators |
| `PUT` | `/api/v1/repos/:user/:repo/members/:member` | Bearer (owner) | Add or update a collaborator: `{"can_push": true}` |
| `DELETE` | `/api/v1/repos/:user/:repo/members/:member` | Bearer (owner) | Remove a collaborator |

`can_push: false` = read-only (clone/fetch only). `can_push: true` = read + write. The owner always has full access regardless of the members table.

### Commits & tree

All endpoints below respect visibility: private repos return 404 for callers without read access.

| Method | Path | Auth | Notes |
|--------|------|------|-------|
| `GET` | `/api/v1/repos/:user/:repo/commits` | Bearer (optional) | `?ref=HEAD&limit=30&offset=0` |
| `GET` | `/api/v1/repos/:user/:repo/commits/:sha` | Bearer (optional) | Single commit |
| `GET` | `/api/v1/repos/:user/:repo/tree/:ref` | Bearer (optional) | Root directory listing |
| `GET` | `/api/v1/repos/:user/:repo/tree/:ref/:path` | Bearer (optional) | Subdirectory listing |
| `GET` | `/api/v1/repos/:user/:repo/blob/:ref/:path` | Bearer (optional) | Raw file content |

### CAD / 3D viewer

| Method | Path | Auth | Notes |
|--------|------|------|-------|
| `GET` | `/api/v1/repos/:user/:repo/glb/:sha/:path` | — | Binary GLB file (or 302 redirect to SeaweedFS) |
| `GET` | `/api/v1/repos/:user/:repo/glb/:sha/:path/status` | — | `{"status": "pending"\|"processing"\|"ready"\|"error"}` |

The GLB status endpoint is polled by the frontend every 2 seconds. Once `ready`, the viewer loads the GLB directly from SeaweedFS via a 302 redirect (the C++ server is not in the download path).

### Repo info response shape

```json
{
  "name": "myrepo",
  "owner": "alice",
  "description": "",
  "clone_url": "http://localhost/alice/myrepo.git",
  "default_branch": "main",
  "empty": false,
  "private": false
}
```

## Configuration

### Server environment variables

All variables are optional; defaults are shown.

| Variable | Default | Description |
|----------|---------|-------------|
| `G4C_HOST` | `0.0.0.0` | Bind address for the HTTP server |
| `G4C_PORT` | `3000` | Bind port for the HTTP server |
| `G4C_DATA_DIR` | `./data` | Root directory for repos, `users.db`, and `repos.db` |
| `G4C_CAD_WORKERS` | `4` | Number of parallel CAD conversion threads |
| `G4C_DEBUG` | `false` | Enable verbose logging (`true`, `1`, or `yes`) |
| `G4C_PUBLIC_URL` | *(empty)* | Public base URL used in clone URLs shown to users (e.g. `https://git4cad.example.com`). When empty, falls back to `http://{G4C_HOST}:{G4C_PORT}` |
| `G4C_SEAWEEDFS_FILER_URL` | `http://seaweedfs:8888` | Internal URL of the SeaweedFS filer used for GLB uploads and existence checks |
| `G4C_SEAWEEDFS_PUBLIC_PREFIX` | `/seaweed` | URL prefix under which nginx proxies SeaweedFS to the browser. The server issues `302 Location: {prefix}/glb/...` for GLB downloads |

### Frontend environment variable

| Variable | Default | Description |
|----------|---------|-------------|
| `VITE_API_URL` | *(empty)* | Backend base URL used by the SvelteKit frontend at build time. Empty means same-origin (correct when nginx proxies everything). Set this only if the frontend is served from a different origin than the API |

### Data layout on the host

```
data/
  users.db          # user database (username, password hash, token) — SQLite
  repos.db          # repository visibility and collaborator table — SQLite
  repos/
    alice/
      myrepo.git/   # bare git repository
    bob/
      parts.git/
  seaweedFS/
```

GLB blobs are stored inside SeaweedFS. They are keyed by commit SHA and file path hash and are immutable once written.

## Docker Compose services

| Service | Image | Internal port | Purpose |
|---------|-------|--------------|---------|
| `nginx` | `nginx:1.27-alpine` | 80 (published) | Reverse proxy / public entrypoint |
| `server` | built from `Dockerfile.server` | 3000 | C++ git server + REST API + CAD pipeline |
| `frontend` | built from `Dockerfile.frontend` | 3001 | SvelteKit web UI |
| `seaweedfs` | `chrislusf/seaweedfs:latest` | 8888 (filer) | GLB blob store |

### nginx routing

| Path pattern | Upstream | Notes |
|---|---|---|
| `/seaweed/*` | SeaweedFS filer | Strips `/seaweed` prefix; sets `Cache-Control: immutable` |
| `/:user/:repo.git/*` | C++ server | Git smart HTTP (clone/push/fetch) |
| `/api/*` | C++ server | REST API |
| `/*` | SvelteKit frontend | All other paths |

### Volumes

| Volume | Type | Contents |
|--------|------|----------|
| `./data` | host bind mount | git repositories + `users.db` + `repos.db` — survives restarts |

## Building locally (without Docker)

### Server

Requires: CMake ≥ 3.22, a C++23 compiler, and either vcpkg or the Ubuntu apt packages listed in `Dockerfile.server`.

```bash
cd server
# With vcpkg:
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
# Or on Ubuntu 22.04 with apt packages installed:
cmake -B build
cmake --build build -j$(nproc)
./build/git4cad-server
```

### Frontend

```bash
cd frontend
npm install
npm run dev       # dev server at http://localhost:5173 (proxies /api to :3000)
npm run build     # production build → build/
npm run check     # TypeScript type check
```

## Debugging the server in CLion (Docker toolchain)

Open Clion and create a new project from source in the server directory (root file CMakeLists.txt)!
  
The `clion-dev` service in `docker-compose.yml` builds a full C++ toolchain
image (`git4cad-clion-dev`) from `Dockerfile.server`'s `builder` stage —
cmake, ninja, gdb/gdbserver, and every dependency, pre-built. It's gated
behind the `dev` compose profile, so a plain `docker compose up` never
starts it.

CLion can't point a Toolchain directly at a `docker-compose.yml` service —
only at a plain image — so the compose service exists to *build* that image
reproducibly, and CLion is configured to use it directly via `docker run`
style flags.

### 1. Build the image

```bash
docker compose build clion-dev
```

### 2. Add the Docker toolchain

**Settings → Build, Execution, Deployment → Toolchains → + → Docker**

- **Image**: `git4cad-clion-dev:latest`
- **Container Settings**:
  ```
  --entrypoint= --rm -v <repo>/server:/src -v <repo>/data:/data
  --network git4cad_internal --cap-add=SYS_PTRACE
  --security-opt seccomp=unconfined
  -e G4C_DATA_DIR=/data -e G4C_HOST=0.0.0.0 -e G4C_PORT=3000
  -e G4C_CAD_WORKERS=4 -e G4C_DEBUG=true
  -e G4C_SEAWEEDFS_FILER_URL=http://seaweedfs:8888
  -e G4C_SEAWEEDFS_PUBLIC_PREFIX=/seaweed
  ```
  Deliberately no `-p` — CLion talks to the container via the Docker
  API/`exec`, not a published port. Publishing one just risks colliding
  with something else on that host port.
- `--network git4cad_internal` must match your actual compose project's
  network name (check `docker network ls` if you've renamed the project
  directory — compose derives it from the folder name).

**⚠️ `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` is required, not optional.**
Without it, gdb can't `ptrace` the debuggee — Docker's default seccomp
profile and dropped capabilities block it — and CLion surfaces this as a
generic **"Unsupported execution environment"** error on Debug, with no
mention of ptrace at all. If you hit that error, check these flags first
before anything else (execution targets, run targets, etc. are not the
cause here).

Open **`server/`** (not the repo root) as the CLion project root, so its
`CMakeLists.txt` lines up with `/src` above.

### 3. Add a GDB Debug Profile

CLion 2026.2+ moved the debugger out of the toolchain into a separate
**Debug Profile**, paired independently in the toolbar:

**Settings → Build, Execution, Deployment → Debugger → Debug Profiles** →
**+** → **GDB** → leave **Executable** empty (CLion resolves `gdb` from the
toolchain's image automatically) → name it, e.g., "Docker GDB".

In the main toolbar, pick your Docker-toolchain **CMake Profile** and this
**Debug Profile** — they pair independently, so any CMake profile can be
run with any compatible debugger.

### 4. Build and debug

Build the `git4cad-server` target (**⌘F9** or the hammer icon), set a
breakpoint, and hit **Debug**.

### Troubleshooting

- **CMake/Compiler/Debugger all say "Not found"**: the toolchain's
  **Image** field is probably pointing at `git4cad-server:latest` (the slim
  *production* runtime image — no compiler, no cmake) instead of
  `git4cad-clion-dev:latest` (the full `builder`-stage image).
- **`IMPORTED_LOCATION not set for imported target "libzip::zip"` /
  `"SQLiteCpp"` on a Debug configure**: `libzip` and `SQLiteCpp` are built
  from source in `Dockerfile.server` with a fixed `CMAKE_BUILD_TYPE=Release`,
  so they only ever publish `IMPORTED_LOCATION_RELEASE`. `CMakeLists.txt`'s
  `CMAKE_MAP_IMPORTED_CONFIG_DEBUG`/`_MINSIZEREL` fall back to `Release` for
  exactly this reason — if you see this error, that mapping is missing or
  was reverted.
- **`docker compose build` fails with `apt-get`... `GPG error: ... At least
  one invalid signature was encountered`**: before chasing proxies or
  keyrings, run `docker system df`. This has been caused by Docker
  Desktop's virtual disk filling up completely, which silently corrupts
  files `apt-get update` writes and surfaces as a signature-verification
  failure that has nothing to do with GPG keys. `docker builder prune -af`
  fixes it.
- **Stuck build directory / weird CMake errors after switching
  toolchains**: delete `server/cmake-build-debug` (gitignored, pure build
  output) and reload the CMake project — a build directory configured
  against a different/broken toolchain can leave stale state that a fresh
  reconfigure doesn't fully clear.
- **A leftover container has some host port bound and a fresh CLion build
  fails with `port is already allocated`**: CLion's Docker toolchain spins
  up a throwaway container per build; a crashed or failed one can leave the
  port claimed. `docker ps -a` to find it, `docker rm` it. Simplest fix if
  you don't actually need host access to the debug binary: drop any `-p`
  mapping from Container Settings entirely (see step 2 — it's not needed
  for CLion to build/run/debug).

## Architecture notes

- **Git protocol**: Crow validates auth, then forks `git http-backend` as a CGI subprocess, proxying stdin/stdout through pipes. After a successful `git-receive-pack`, libgit2 walks new commits to find CAD file blobs and enqueues them for conversion.
- **CAD pipeline**: A fixed thread pool (`G4C_CAD_WORKERS`) drains a `std::deque<CadJob>`. Conversion status is tracked in an in-memory map; SeaweedFS HEAD requests provide a restart-safe fallback.
- **STEP → GLB**: `STEPControl_Reader` → `TopoDS_Shape` → XDE document → `RWGltf_CafWriter`
- **FCStd → GLB**: libzip extract → pugixml parse `Document.xml` → `BRepTools::Read` per BRep file → quaternion placements via `gp_Trsf` → XDE assembly → `RWGltf_CafWriter`
- **GLB delivery**: The C++ server returns a `302` redirect to `/seaweed/glb/...`; nginx proxies that path directly to SeaweedFS so the C++ server handles zero bytes of the download.
- **Tokens**: One static token per user, generated at registration with 32 bytes of `RAND_bytes`. Passwords are stored as `salt:SHA-256(salt+password)`.
- **Visibility**: Repo metadata is tracked in `repos.db` (SQLite). Repos not recorded there (e.g. manually created bare repos) are treated as public with push restricted to the URL path owner. The `PATCH /repos/:u/:r` endpoint toggles the flag at runtime without touching the filesystem.
