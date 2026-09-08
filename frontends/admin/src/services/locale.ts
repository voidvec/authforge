/**
 * Active-UI-locale state for the service layer.
 *
 * Dependency-free on purpose: `errorAdapter.ts` and `services/messages/`
 * are mirrored across frontends/user and frontends/admin and are imported
 * by node-env unit tests (vitest) — they must not touch the DOM or pull in
 * app-only modules (vue-i18n, catalogs). `src/i18n/index.ts` owns
 * detection/persistence, so one switch drives both page chrome and error
 * messages (ADR-0013).
 *
 * Reactive resolution (#158): when the app wires vue-i18n, `src/i18n/index.ts`
 * installs a locale getter via `setLocaleGetter(() => i18n.global.locale.value)`.
 * Because the composer locale is a ref (legacy: false), a `getCurrentLocale()`
 * call made inside a Vue render effect (e.g. a `computed` resolving an error
 * message) registers a dependency and re-resolves when the locale switches.
 * The plain `current` mirror stays as the fallback for getter-less importers
 * (node-env unit tests, the cross-app property test) — those keep the exact
 * pre-#158 behavior.
 */
export type AppLocale = 'en' | 'zh-CN'

export const SUPPORTED_LOCALES: readonly AppLocale[] = ['en', 'zh-CN']

/** Fallback locale used when a requested locale table is missing. */
export const FALLBACK_LOCALE: AppLocale = 'en'

let current: AppLocale = FALLBACK_LOCALE

let localeGetter: (() => AppLocale) | null = null

/**
 * Install the reactive locale source (called once from `src/i18n/index.ts`).
 * Pass `null` to drop it and fall back to the plain mirror — unit tests do
 * this in afterEach so module state never leaks between cases.
 */
export function setLocaleGetter(getter: (() => AppLocale) | null): void {
  localeGetter = getter
}

export function getCurrentLocale(): AppLocale {
  return localeGetter ? localeGetter() : current
}

export function setCurrentLocale(locale: AppLocale): void {
  current = locale
}
