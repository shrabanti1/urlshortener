// docs/openapi.yaml is the source of truth. Copy it into public/ before every
// build so the served spec cannot drift from the documented one.
import { copyFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const src = resolve(here, "../../docs/openapi.yaml");
const dest = resolve(here, "../public/docs/openapi.yaml");
mkdirSync(dirname(dest), { recursive: true });
copyFileSync(src, dest);
console.log("synced openapi.yaml -> public/docs/");
