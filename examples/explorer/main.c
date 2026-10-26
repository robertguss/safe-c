/* The Postgres explorer (docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md).
 * U1 is the scaffold: the binary links SDL3, SDL3_ttf, libpq and the standard
 * library and exits. U2 moves the C `main` into adapters/ui.c (KTD17), which
 * calls `app_main` here. */

int main(void) {
    return 0;
}
