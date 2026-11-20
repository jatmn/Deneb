/**
 * SPDX-License-Identifier: MPL-2.0
 * Host-only ESLint config for Deneb's existing browser scripts.
 * Paths are relative to the repository root, which is the lint working directory.
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
]);
