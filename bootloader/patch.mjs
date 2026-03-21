import fs from 'fs';
import path from 'path';
import { pathToFileURL, fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// --- Load patch file ---
const patchFileArg = process.argv[2];

if (!patchFileArg) {
    console.error('ERROR: No patch file provided');
    process.exit(1);
}

const patchFilePath = path.resolve(__dirname, patchFileArg);

if (!fs.existsSync(patchFilePath)) {
    console.error(`ERROR: Patch file not found: ${patchFileArg}`);
    process.exit(1);
}

let patch;
try {
    const mod = await import(pathToFileURL(patchFilePath));
    patch = mod.patch;
} catch (e) {
    console.error(`ERROR: Failed to load patch file: ${e.message}`);
    process.exit(1);
}

if (!Array.isArray(patch)) {
    console.error('ERROR: Patch file must export an array named "patch"');
    process.exit(1);
}

// --- Apply patches ---
for (const p of patch) {
    const filePath = path.join(__dirname, p.file);

    if (!fs.existsSync(filePath)) {
        console.error(`ERROR: File not found: ${p.file}`);
        process.exit(1);
    }

    let content = fs.readFileSync(filePath, 'utf8');

    if (Array.isArray(p.replace)) {
        const searchFlags = p.search.flags.includes('g') ? p.search.flags : `${p.search.flags}g`;
        const searchRegex = new RegExp(p.search.source, searchFlags);
        const matches = [...content.matchAll(searchRegex)];

        if (matches.length !== p.replace.length) {
            console.error(
                `ERROR: Match count (${matches.length}) does not equal replacement array length (${p.replace.length}) in file ${p.file}`
            );
            process.exit(1);
        }

        // Build result without re-matching replaced content
        let result = '';
        let lastIndex = 0;

        matches.forEach((match, i) => {
            const start = match.index;
            const end = start + match[0].length;

            result += content.slice(lastIndex, start);
            result += p.replace[i];
            lastIndex = end;
        });

        result += content.slice(lastIndex);
        content = result;

    } else {
        const hasMatch = p.search.test(content);

        if (!hasMatch) {
            console.error(`ERROR: No matches found in file ${p.file}`);
            process.exit(1);
        }

        if (p.search.global) {
            p.search.lastIndex = 0;
        }

        content = content.replace(p.search, p.replace);
    }

    fs.writeFileSync(filePath, content, 'utf8');
    console.log(`Patched: ${p.file}`);
}

console.log('Done.');