// #158: the services layer stays vue-i18n-free, but the app injects a reactive
// locale getter (src/i18n/index.ts: setLocaleGetter(() => i18n.global.locale
// .value)) so getErrorMessage resolves in the active UI locale. Inside a Vue
// render effect the getter's ref read is what makes resolved messages track
// locale switches. These cases pin the injected-getter contract and the
// getter-less fallback that node-env tests rely on.
import { afterEach, describe, expect, it } from 'vitest'
import { FALLBACK_LOCALE, getCurrentLocale, setCurrentLocale, setLocaleGetter } from './locale'
import { getErrorMessage } from './messages'

afterEach(() => {
  // Module state must never leak between suites: drop the getter and restore
  // the plain mirror the getter-less importers see.
  setLocaleGetter(null)
  setCurrentLocale(FALLBACK_LOCALE)
})

describe('reactive locale injection (#158)', () => {
  it('falls back to the plain mirror when no getter is installed', () => {
    expect(getCurrentLocale()).toBe(FALLBACK_LOCALE)
    setCurrentLocale('zh-CN')
    expect(getCurrentLocale()).toBe('zh-CN')
    expect(getErrorMessage('AUTH_INVALID_CREDENTIALS')).toBe('用户名或密码错误')
  })

  it('prefers the injected getter and re-resolves messages on switch', () => {
    setCurrentLocale('en')
    let active: 'en' | 'zh-CN' = 'en'
    setLocaleGetter(() => active)

    expect(getErrorMessage('AUTH_INVALID_CREDENTIALS')).toBe('Incorrect username or password')

    // Simulates the vue-i18n locale ref flipping under the getter — the exact
    // read getErrorMessage performs inside a render effect.
    active = 'zh-CN'
    expect(getCurrentLocale()).toBe('zh-CN')
    expect(getErrorMessage('AUTH_INVALID_CREDENTIALS')).toBe('用户名或密码错误')
  })

  it('getter result wins over a stale mirror value', () => {
    setCurrentLocale('zh-CN') // stale mirror never updated by the app
    setLocaleGetter(() => 'en')
    expect(getErrorMessage('AUTH_INVALID_CREDENTIALS')).toBe('Incorrect username or password')
  })
})
