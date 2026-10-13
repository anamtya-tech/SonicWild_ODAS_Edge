# Contributing

## Branches

| Branch | Purpose |
|--------|---------|
| `main` | Stable baseline |
| `makeshDev` | Active integration branch for current fork |
| `dev/*` | Optional short-lived feature branches |

Merge flow (recommended): `dev/*` → PR → `makeshDev` → periodic merge to `main`.

## Syncing with upstream ODAS

```bash
git fetch upstream
git checkout makeshDev
git merge upstream/master          # resolve conflicts in CMakeLists, src/module/
git push origin makeshDev
```

Changes to the upstream core (SSL, SSS, beamformer) should be kept minimal to
make future merges tractable.  Chatak-specific code lives in:

- `src/yamnet/` — YAMNet wrapper
- `src/module/mod_sst.c` — SST event pipeline
- `src/connector/con_chatak_id.c` — Chatak-ID connector
- `src/sink/snk_tracks.c` — track JSON output and sidecar emission
- `include/yamnet/`, `include/odas/*chatak*` — headers

## Commit style

```
<type>(<scope>): short description

Body (optional).
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`  
Scopes: `sst`, `yamnet`, `ssl`, `config`, `docs`, `ci`

## Updating the model

1. Export a new `yamnet_core.tflite` from `anamtya-tech/yamnet`
2. Copy into `models/yamnet_core.tflite`
3. Run the integration test: `python3 scripts/vm_socket_emit.py --test`
4. Commit with `chore(yamnet): update model to <date>`
