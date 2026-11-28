/**
 * SPDX-License-Identifier: MPL-2.0
 * Host-only ESLint config for Deneb's existing browser scripts and the
 * host-side Node.js scripts and tests that check them.
 * Paths are relative to the repository root, which is the lint working directory.
 * ESLint applies its default file patterns with no rules to any .js, .mjs, or
 * .cjs file outside these blocks, so every tracked script needs a block here.
 */
import js from "@eslint/js";
import { defineConfig, globalIgnores } from "eslint/config";
import globals from "globals";

export default defineConfig([
    // The website build downloads the checksum-pinned FlexSearch bundle here.
    globalIgnores(["website/assets/js/vendor/"]),
    {
        files: ["web/www/js/**/*.js", "website/assets/js/**/*.js"],
        extends: [js.configs.recommended],
        languageOptions: {
            ecmaVersion: 5,
            sourceType: "script",
            globals: {
                ...globals.browser,
            },
        },
        rules: {
            // These scripts are ES5. `var` is function-scoped, so a later
            // `for (var i ...)` reuses the same binding on purpose.
            "no-redeclare": "off",
            "no-empty": ["error", { allowEmptyCatch: true }],
            "no-unused-vars": ["error", {
                caughtErrors: "none",
            }],
        },
    },
    {
        // Host-only Node.js ES modules: CI helpers and this config.
        files: ["scripts/**/*.mjs", "tools/eslint/eslint.config.js"],
        extends: [js.configs.recommended],
        languageOptions: {
            sourceType: "module",
            globals: {
                ...globals.node,
            },
        },
    },
    {
        // Host-only Node.js CommonJS tests run by the web CMake test suite.
        files: ["web/tests/**/*.cjs"],
        extends: [js.configs.recommended],
        languageOptions: {
            sourceType: "commonjs",
            globals: {
                ...globals.node,
            },
        },
    },
]);
