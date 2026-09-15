/* Shared, operation-local HTTP challenge UI. Never owns or displays secrets. */
#include "nav_ui.h"
#include "nav_ui_core.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool attempted(const NavHttpAuthAttempts *attempts, const char *name)
{
    for (size_t i = 0; i < attempts->count; i++)
        if (!strcmp(attempts->names[i], name)) return true;
    return false;
}
static void note_attempt(NavHttpAuthAttempts *attempts, const char *name)
{
    if (name[0] && !attempted(attempts, name) && attempts->count < NAV_CREDENTIAL_MAX)
        snprintf(attempts->names[attempts->count++], NAV_CREDENTIAL_NAME_MAX, "%s", name);
}
static void message(const char *text)
{ const char *lines[] = {text}; nav_ui_info(" Authentication Required ", lines, 1); }

int nav_ui_unlock_vault(NavCredentialStore *store, char *error, size_t size,
                         NavUiRedrawFn redraw, void *data)
{
    char password[1024] = {0};
    int result = 0;
    if (!nav_ui_prompt_secret(" Unlock Vault ", "Master password: ", password,
                              sizeof password, redraw, data))
        result = nav_credential_store_unlock(store, password, error, size) ? -1 : 1;
    nav_credential_secret_wipe(password, sizeof password);
    return result;
}

bool nav_ui_http_auth_retry(NavApp *app, NavProvider **provider, bool *owned,
    const char *url, NavHttpAuthAttempts *attempts, const char *failure,
    NavUiRedrawFn redraw, void *data)
{
    if (!app || !nav_http_authentication_needed(*provider)) return false;
    if (app->credential_store && !nav_credential_store_is_locked(app->credential_store))
        note_attempt(attempts, nav_http_credential_name(*provider));
    char error[256] = {0};
    if (!app->credential_store) {
        char directory[NAV_PATH_MAX], path[NAV_PATH_MAX];
        if (nav_platform_config_dir(directory, sizeof directory) ||
            snprintf(path, sizeof path, "%s/vault.bin", directory) >= (int)sizeof path ||
            nav_platform_access(path, F_OK) ||
            nav_credential_store_open_vault(&app->credential_store, path, error, sizeof error)) {
            message(error[0] ? error : "No Vault exists. Store a credential in Vault first."); return false;
        }
    }
    const char *scopes[] = {"Use once", "Remember URL root", "Add as Repository"};
    int credential = 0, scope = 0, focus = 0, button = 0;
    bool initial = true;
    for (;;) {
        const char *labels[NAV_CREDENTIAL_MAX];
        char lines[NAV_CREDENTIAL_MAX][NAV_CREDENTIAL_NAME_MAX + 32];
        const NavCredential *records[NAV_CREDENTIAL_MAX];
        size_t count = 0;
        bool locked = nav_credential_store_is_locked(app->credential_store);
        unsigned types = nav_http_authentication_types(*provider);
        if (!locked) for (size_t i = 0; i < nav_credential_store_count(app->credential_store); i++) {
            const NavCredential *record = nav_credential_store_get(app->credential_store, i);
            if (record->type != NAV_CREDENTIAL_BASIC && record->type != NAV_CREDENTIAL_BEARER) continue;
            if (!(types & (1u << (record->type - 1)))) continue;
            records[count] = record;
            snprintf(lines[count], sizeof lines[count], "%s%s", record->name,
                     attempted(attempts, record->name) ? " [already attempted]" : "");
            labels[count] = lines[count]; count++;
        }
        if (initial && count) {
            for (size_t i = 0; i < count; i++) if (!attempted(attempts, records[i]->name)) { credential = (int)i; break; }
            initial = false;
        }
        if ((size_t)credential >= count) credential = 0;
        if (redraw) redraw(data); else nav_term_clear(NAV_STYLE_BACKGROUND);
        int width = nav_term_width(), height = nav_term_height();
        int w = width > 90 ? 90 : width - 2, h = 11;
        int x = (width - w) / 2, y = (height - h) / 2;
        if (w < 32 || height < h) nav_ui_text(0, 0, width, "Resize terminal for authentication", NAV_STYLE_MESSAGE);
        else {
            nav_ui_box(x, y, w, h, " Authentication Required ", NAV_STYLE_DIALOG);
            nav_ui_text(x + 2, y + 2, w - 4, url, NAV_STYLE_DIALOG);
            nav_ui_text(x + 2, y + 3, w - 4, failure, NAV_STYLE_ERROR);
            const char *last = nav_http_credential_name(*provider);
            if (last[0] && attempted(attempts, last)) {
                char text[NAV_CREDENTIAL_NAME_MAX + 32];
                snprintf(text, sizeof text, "Credential already attempted: %s", last);
                nav_ui_text(x + 2, y + 4, w - 4, text, NAV_STYLE_TEXT_DIM);
            }
            nav_ui_text(x + 2, y + 5, 12, "Credential:", NAV_STYLE_DIALOG);
            nav_ui_select_draw(x + 14, y + 5, w - 16, locked ? "Vault locked: Unlock..." :
                count ? labels[credential] : "No compatible Vault credentials", "", focus == 0 ? NAV_STYLE_SELECTION : NAV_STYLE_TEXT);
            nav_ui_text(x + 2, y + 6, 12, "Scope:", NAV_STYLE_DIALOG);
            nav_ui_select_draw(x + 14, y + 6, w - 16, scopes[scope], "", focus == 1 ? NAV_STYLE_SELECTION : NAV_STYLE_TEXT);
            nav_ui_text(x + 4, y + 9, (w - 8) / 2, "Retry", focus == 2 && !button ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
            nav_ui_text(x + w / 2, y + 9, (w - 8) / 2, "Cancel", focus == 2 && button ? NAV_STYLE_SELECTION : NAV_STYLE_DIALOG);
        }
        nav_term_hide_cursor(); nav_term_present();
        NavAction action;
        if (nav_ui_input(NAV_CONTEXT_DIALOG, &action) <= 0 || action.type != NAV_TERM_EVENT_KEY) continue;
        if (action.command == NAV_CMD_CANCEL) return false;
        if (action.command == NAV_CMD_DOWN || action.command == NAV_CMD_PANEL_SWITCH) { focus = (focus + 1) % 3; continue; }
        if (action.command == NAV_CMD_UP) { focus = (focus + 2) % 3; continue; }
        if (focus == 2 && (action.command == NAV_CMD_LEFT || action.command == NAV_CMD_RIGHT)) { button = !button; continue; }
        if (action.command != NAV_CMD_ACCEPT) continue;
        if (focus == 0) {
            if (locked) {
                if (nav_ui_unlock_vault(app->credential_store, error, sizeof error, redraw, data) < 0) message(error);
                initial = true;
            } else if (count && nav_ui_select(" Vault Credential ", labels, count, &credential, NAV_CONTEXT_PICKER, redraw, data)) focus = 1;
            continue;
        }
        if (focus == 1) {
            if (nav_ui_select(" Credential Scope ", scopes, 3, &scope, NAV_CONTEXT_PICKER, redraw, data)) focus = 2;
            continue;
        }
        if (button) return false;
        if (locked) { focus = 0; continue; }
        if (!count) { message("No compatible credential. Add Basic/Bearer credentials in Vault."); continue; }
        NavRepository settings;
        nav_http_provider_settings(*provider, &settings);
        snprintf(settings.credential, sizeof settings.credential, "%s", records[credential]->name);
        if (attempted(attempts, settings.credential) &&
            !nav_ui_confirm("This credential was already attempted. Retry it manually?", redraw, data)) continue;
        if (scope) {
            if (nav_http_scope_suggest(url, settings.url, sizeof settings.url)) { message("Unable to suggest a safe root"); continue; }
            if (scope == 1) {
                if (nav_ui_prompt_text(" Credential Scope ", "Remember URL root: ", settings.url,
                                      sizeof settings.url, redraw, data)) continue;
                char root[NAV_URL_MAX];
                if (nav_http_scope_normalize(settings.url, url, root, sizeof root, error, sizeof error)) { message(error); continue; }
                snprintf(settings.url, sizeof settings.url, "%s", root);
                /* Show the canonical root again, including an explicitly entered origin-wide scope. */
                char confirmation[NAV_URL_MAX + 64];
                snprintf(confirmation, sizeof confirmation, "Save credential scope %s and retry?", root);
                if (!nav_ui_confirm(confirmation, redraw, data)) continue;
                if (nav_http_auth_scope_remember(&app->config, &settings, url, error, sizeof error)) { message(error); continue; }
            } else {
                snprintf(settings.name, sizeof settings.name, "HTTP resource");
                if (!nav_ui_repository_add_prefilled(app, &settings, url)) continue;
            }
        }
        /* Even remembered credentials stay resource-bound in this operation.
         * Future operations resolve the newly saved scope normally. */
        NavProvider *retry = nav_http_authentication_retry_provider(*provider, app->credential_store,
            url, settings.credential, &settings, error, sizeof error);
        if (!retry) { message(error[0] ? error : "Unable to retry authentication"); continue; }
        note_attempt(attempts, settings.credential);
        if (*owned) nav_provider_destroy(*provider);
        *provider = retry; *owned = true; return true;
    }
}
