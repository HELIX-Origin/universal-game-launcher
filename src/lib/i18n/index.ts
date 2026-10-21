// Library-driven i18n localization engine for Universal Game Launcher powered by i18next
// and dynamically backed by the HELIX Gaming DB REST API (https://api.helix-origin.club).

import i18next from "i18next";
import { derived, writable } from "svelte/store";
import { api } from "$lib/api";
import { findLanguage, STEAM_LANGUAGES } from "./languages";
import { en } from "./locales";
import type { SteamLanguage } from "./types";

export * from "./languages";
export * from "./types";

const STORAGE_KEY = "universal_game_launcher_language";
const LEGACY_STORAGE_KEY = "steam_vault_language";

const GATEWAYS = [
  "https://api.helix-origin.club",
  "https://api.helix-origin.club/ugl-db-00",
  "https://raw.githubusercontent.com/HELIX-Origin/ugl-db-00/main"
];

/** Get initial language choice from storage, browser preference, or default to English. */
function getInitialLocale(): string {
  if (typeof window !== "undefined") {
    try {
      const stored = localStorage.getItem(STORAGE_KEY) ?? localStorage.getItem(LEGACY_STORAGE_KEY);
      if (stored) return findLanguage(stored).id;
    } catch {
      // Storage unavailable or blocked
    }

    if (navigator?.language) {
      const matched = findLanguage(navigator.language);
      return matched.id;
    }
  }
  return "en";
}

const initialLang = getInitialLocale();

// Initialize i18next synchronously with baseline English so initial render is instantaneous
i18next.init({
  lng: initialLang,
  fallbackLng: "en",
  ns: ["app", "genres", "categories", "platforms", "common"],
  defaultNS: "app",
  fallbackNS: "app",
  interpolation: {
    escapeValue: false,
    prefix: "{",
    suffix: "}"
  },
  resources: {
    en: {
      app: en as unknown as Record<string, any>
    }
  },
  returnEmptyString: false
});

/** Reactive store for currently selected language code (e.g. "en", "de", "zh-CN", "ja"). */
export const locale = writable<string>(initialLang);

/** Internal reactive version store to signal Svelte components when translations are loaded. */
const i18nVersion = writable<number>(0);

/** Reactive store returning full metadata for the currently active language. */
export const activeLanguage = derived<typeof locale, SteamLanguage>(locale, ($loc) => findLanguage($loc));

/** Reactive store indicating whether current language requires RTL (Right-to-Left) direction. */
export const isRTL = derived<typeof activeLanguage, boolean>(activeLanguage, ($lang) => Boolean($lang.rtl));

// Set of loaded language codes
const loadedLocales = new Set<string>(["en"]);
const inFlightRequests = new Map<string, Promise<boolean>>();

/**
 * Fetch localization bundle dynamically from HELIX Gaming DB candidate gateways.
 */
async function fetchRemoteBundle(langCode: string): Promise<Record<string, any> | null> {
  const relativePath = `/api/v2/i18n/${langCode}/bundle.json`;
  for (const base of GATEWAYS) {
    try {
      const cleanBase = base.endsWith("/") ? base.slice(0, -1) : base;
      const url = `${cleanBase}${relativePath}`;
      const data = await api.fetchExternalJson<Record<string, any>>(url);
      if (data && (data.app || data.genres || data.common)) {
        return data;
      }
    } catch {
      // Try next gateway
    }
  }
  return null;
}

/**
 * Ensures the target language's translation resources are loaded into i18next,
 * pulling from offline persistent KV cache or fetching live from the HELIX Gaming DB API.
 */
export async function ensureLocaleLoaded(langCode: string): Promise<boolean> {
  const target = findLanguage(langCode).id;
  if (loadedLocales.has(target)) {
    return true;
  }

  if (inFlightRequests.has(target)) {
    return inFlightRequests.get(target)!;
  }

  const loadPromise = (async () => {
    const kvKey = `i18n_bundle_${target}`;

    // 1. Try local offline cache first for instant response
    try {
      const cached = await api.kvGetJson<Record<string, any>>(kvKey);
      if (cached && (cached.app || cached.genres)) {
        injectBundle(target, cached);
      }
    } catch {
      // Non-fatal
    }

    // 2. Fetch live bundle from HELIX Gaming DB
    try {
      const liveBundle = await fetchRemoteBundle(target);
      if (liveBundle) {
        injectBundle(target, liveBundle);
        try {
          await api.kvSetJson(kvKey, liveBundle, 86400 * 14); // 14-day retention
        } catch {
          // Non-fatal
        }
        return true;
      }
    } catch {
      // Network offline; proceed with whatever was loaded
    }

    return loadedLocales.has(target);
  })();

  inFlightRequests.set(target, loadPromise);
  try {
    const result = await loadPromise;
    return result;
  } finally {
    inFlightRequests.delete(target);
  }
}

function injectBundle(lang: string, bundle: Record<string, any>): void {
  if (bundle.app) {
    i18next.addResourceBundle(lang, "app", bundle.app, true, true);
  }
  if (bundle.genres) {
    i18next.addResourceBundle(lang, "genres", bundle.genres, true, true);
  }
  if (bundle.categories) {
    i18next.addResourceBundle(lang, "categories", bundle.categories, true, true);
  }
  if (bundle.platforms) {
    i18next.addResourceBundle(lang, "platforms", bundle.platforms, true, true);
  }
  if (bundle.common) {
    i18next.addResourceBundle(lang, "common", bundle.common, true, true);
  }
  loadedLocales.add(lang);
  i18nVersion.update((n) => n + 1);
}

/**
 * Resolve translation for a given dot-notation key and optional parameter interpolation.
 */
export function translate(
  key: string,
  params?: Record<string, string | number>,
  langCode?: string
): string {
  const currentLang = langCode || i18next.language || "en";
  const result = i18next.t(key, {
    lng: currentLang,
    defaultValue: key,
    ...params
  });
  return typeof result === "string" ? result : key;
}

/**
 * Reactive translation function store `$t`.
 * In Svelte components: `const { t } = ...; $t("nav.library")`.
 */
export const t = derived([locale, i18nVersion], ([$loc]) => {
  return (key: string, params?: Record<string, string | number>): string => {
    return translate(key, params, $loc);
  };
});

/**
 * Translates a genre term using the library-driven taxonomy.
 */
export function translateGenre(genre: string, langCode?: string): string {
  if (!genre) return "";
  const key = genre.toLowerCase().replace(/[^a-z0-9]+/g, "_");
  const target = langCode || i18next.language || "en";
  const result = i18next.t(key, { ns: "genres", lng: target, defaultValue: genre });
  return typeof result === "string" ? result : genre;
}

/**
 * Translates a category term using the library-driven taxonomy.
 */
export function translateCategory(category: string, langCode?: string): string {
  if (!category) return "";
  const key = category.toLowerCase().replace(/[^a-z0-9]+/g, "_");
  const target = langCode || i18next.language || "en";
  const result = i18next.t(key, { ns: "categories", lng: target, defaultValue: category });
  return typeof result === "string" ? result : category;
}

/**
 * Translates a platform name using the library-driven taxonomy.
 */
export function translatePlatform(platform: string, langCode?: string): string {
  if (!platform) return "";
  const key = platform.toLowerCase().replace(/[^a-z0-9]+/g, "_");
  const target = langCode || i18next.language || "en";
  const result = i18next.t(key, { ns: "platforms", lng: target, defaultValue: platform });
  return typeof result === "string" ? result : platform;
}

/**
 * Switch active locale, load remote resources dynamically, and update DOM direction.
 */
export function setLocale(newLocale: string): void {
  const lang = findLanguage(newLocale);
  locale.set(lang.id);
  i18next.changeLanguage(lang.id);

  if (typeof window !== "undefined") {
    try {
      localStorage.setItem(STORAGE_KEY, lang.id);
    } catch {
      // Storage unavailable
    }

    if (typeof document !== "undefined") {
      document.documentElement.lang = lang.webCode || lang.id;
      document.documentElement.dir = lang.rtl ? "rtl" : "ltr";
    }
  }

  // Asynchronously ensure the target language bundle is loaded from HELIX Gaming DB
  ensureLocaleLoaded(lang.id).then(() => {
    i18nVersion.update((n) => n + 1);
  });
}

/**
 * Synchronize with backend settings language (called when settings are loaded or saved).
 */
export function syncWithSettingsLanguage(settingsLang: string | null | undefined): void {
  if (!settingsLang) return;
  const target = findLanguage(settingsLang);
  setLocale(target.id);
}

// Kick off initial loading for the active language if not English
if (initialLang !== "en") {
  ensureLocaleLoaded(initialLang).then(() => {
    i18nVersion.update((n) => n + 1);
  });
}
