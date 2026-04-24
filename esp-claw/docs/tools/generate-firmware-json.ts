#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
//
// SPDX-License-Identifier: Apache-2.0

import { promises as fs } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

type MetadataRecord = {
  chip?: unknown;
  board?: unknown;
  merged_binary?: unknown;
  min_flash_size?: unknown;
  nvs_info?: unknown;
};

type FirmwareEntry = {
  board: string;
  features: string[];
  description: string;
  merged_binary: string;
  min_flash_size: number;
  min_psram_size: number;
  nvs_info: {
    start_addr: string;
    size: string;
  };
};

type FirmwareDb = Record<string, FirmwareEntry[]>;

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const DOCS_ROOT = path.resolve(SCRIPT_DIR, "..");
const TARGET_MERGED_DIR = path.join(DOCS_ROOT, "public", "merged_binary");
const TARGET_FIRMWARE_JSON = path.join(DOCS_ROOT, "src", "flash-tool", "firmware.json");

function log(msg: string): void {
  console.error(msg);
}

function parseFlashMB(value: unknown): number {
  if (typeof value === "number" && Number.isInteger(value)) {
    return value;
  }

  if (typeof value !== "string") {
    throw new Error(`unsupported min_flash_size type: ${typeof value}`);
  }

  const text = value.trim();
  if (!text) {
    throw new Error("empty min_flash_size");
  }

  const upper = text.toUpperCase();
  if (upper.endsWith("MB")) {
    return Number.parseInt(upper.slice(0, -2).trim(), 0);
  }
  if (upper.endsWith("M")) {
    return Number.parseInt(upper.slice(0, -1).trim(), 0);
  }

  const num = Number.parseInt(text, 0);
  if (num % (1024 * 1024) === 0) {
    return num / (1024 * 1024);
  }

  return num;
}

async function loadMetadataFiles(mergedDir: string): Promise<MetadataRecord[]> {
  const dirEntries = await fs.readdir(mergedDir, { withFileTypes: true });
  const metadataFiles = dirEntries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
    .map((entry) => entry.name)
    .sort((a, b) => a.localeCompare(b));

  if (metadataFiles.length === 0) {
    throw new Error(`No metadata json found in ${mergedDir}`);
  }

  const records: MetadataRecord[] = [];
  for (const fileName of metadataFiles) {
    const filePath = path.join(mergedDir, fileName);
    const raw = await fs.readFile(filePath, "utf8");
    const data = JSON.parse(raw);
    if (!data || typeof data !== "object" || Array.isArray(data)) {
      throw new Error(`Invalid metadata format: ${filePath}`);
    }
    records.push(data as MetadataRecord);
  }

  return records;
}

async function copyMergedBinaryDir(sourceMergedDir: string): Promise<void> {
  await fs.rm(TARGET_MERGED_DIR, { recursive: true, force: true });
  await fs.mkdir(TARGET_MERGED_DIR, { recursive: true });
  await fs.cp(sourceMergedDir, TARGET_MERGED_DIR, { recursive: true });
}

function usage(): string {
  return [
    "Usage:",
    "  node ./docs/tools/generate-firmware-json.ts <merged_binary_dir> [--allow-empty]",
    "",
    "Example:",
    "  node ./docs/tools/generate-firmware-json.ts ./merged_binary",
    "  node ./docs/tools/generate-firmware-json.ts ./merged_binary --allow-empty",
  ].join("\n");
}

async function main(): Promise<number> {
  const argv = process.argv.slice(2);
  const allowEmpty = argv.includes("--allow-empty");
  const positional = argv.filter((arg) => !arg.startsWith("--"));
  const sourceArg = positional[0]?.trim();
  if (!sourceArg) {
    log("Missing <merged_binary_dir> argument.");
    log(usage());
    return 1;
  }

  const sourceMergedDir = path.resolve(process.cwd(), sourceArg);
  let stat;
  try {
    stat = await fs.stat(sourceMergedDir);
  } catch {
    if (allowEmpty) {
      log(`warning: merged_binary directory not found, skip generation: ${sourceMergedDir}`);
      return 0;
    }

    log(`merged_binary directory not found: ${sourceMergedDir}`);
    return 1;
  }
  if (!stat.isDirectory()) {
    log(`merged_binary path is not a directory: ${sourceMergedDir}`);
    return 1;
  }

  let records: MetadataRecord[];
  try {
    records = await loadMetadataFiles(sourceMergedDir);
  } catch (error) {
    log((error as Error).message);
    return 1;
  }

  await copyMergedBinaryDir(sourceMergedDir);

  const firmware: FirmwareDb = {};
  for (const record of records) {
    const chip = record.chip;
    const board = record.board;
    const mergedBinary = record.merged_binary;
    const minFlashSize = record.min_flash_size;
    const nvsInfo = record.nvs_info;

    if (typeof chip !== "string" || !chip.trim()) {
      log(`skip one metadata: invalid chip (${JSON.stringify(record)})`);
      continue;
    }
    if (typeof board !== "string" || !board.trim()) {
      log(`skip one metadata: invalid board (${JSON.stringify(record)})`);
      continue;
    }
    if (typeof mergedBinary !== "string" || !mergedBinary.trim()) {
      log(`skip one metadata: invalid merged_binary (${JSON.stringify(record)})`);
      continue;
    }
    if (!nvsInfo || typeof nvsInfo !== "object" || Array.isArray(nvsInfo)) {
      log(`skip one metadata: invalid nvs_info (${JSON.stringify(record)})`);
      continue;
    }

    let minFlashMB: number;
    try {
      minFlashMB = parseFlashMB(minFlashSize);
    } catch (error) {
      log(`skip one metadata: invalid min_flash_size (${JSON.stringify(record)}) (${(error as Error).message})`);
      continue;
    }

    const nvsInfoRecord = nvsInfo as Record<string, unknown>;
    const item: FirmwareEntry = {
      board,
      features: [],
      description: "",
      merged_binary: `/merged_binary/${mergedBinary}`,
      min_flash_size: minFlashMB,
      min_psram_size: 8,
      nvs_info: {
        start_addr: String(nvsInfoRecord.start_addr ?? ""),
        size: String(nvsInfoRecord.size ?? ""),
      },
    };

    if (!firmware[chip]) {
      firmware[chip] = [];
    }
    firmware[chip].push(item);
  }

  if (Object.keys(firmware).length === 0) {
    log("No valid metadata collected from merged_binary/*.json");
    return 1;
  }

  for (const chip of Object.keys(firmware)) {
    firmware[chip].sort((a, b) => a.board.localeCompare(b.board));
  }

  await fs.writeFile(TARGET_FIRMWARE_JSON, `${JSON.stringify(firmware, null, 2)}\n`, "utf8");
  console.log(`Copied merged binaries to: ${TARGET_MERGED_DIR}`);
  console.log(`Generated: ${TARGET_FIRMWARE_JSON}`);
  return 0;
}

main()
  .then((code) => {
    process.exitCode = code;
  })
  .catch((error) => {
    log((error as Error).stack ?? String(error));
    process.exitCode = 1;
  });
