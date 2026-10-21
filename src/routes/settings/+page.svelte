<script lang="ts">
  import { onMount } from "svelte";
  import { goto } from "$app/navigation";
  import { api, errorMessage } from "$lib/api";
  import { STEAM_LANGUAGES, setLocale, syncWithSettingsLanguage, t } from "$lib/i18n";
  import { collections, error, games, install, settings, steamRunning } from "$lib/stores";
  import type { CacheStats, Settings } from "$lib/types";
  import PlatformsSection from "$lib/components/settings/PlatformsSection.svelte";
  import { pluginEngine } from "$lib/plugins/engine";
  import "$styles/settings.scss";

  type SettingsTab = "general" | "providers" | "api" | "media" | "storage";
  let activeTab = $state<SettingsTab>("general");

  let form = $state<Settings | null>(null);
  let cacheStats = $state<CacheStats | null>(null);
  let syncingLibraryMeta = $state(false);
  let syncLibraryAutoArtwork = $state(true);
  let syncLibraryMsg = $state<string | null>(null);
  let clearingCache = $state(false);
  let clearCacheMsg = $state<string | null>(null);
  let showClearCacheConfirm = $state(false);
  let busy = $state(false);
  let saved = $state(false);

  // Feature configuration states organized by domain
  let trailersConfig = $state({ autoPlay: false, muted: true });
  let ambientConfig = $state({ volume: 50, loopPlayback: true });
  let gamerpowerConfig = $state({ enableFreeGames: true, enableDeals: true, notifyDesktop: true });
  let storageConfig = $state({ backgroundSync: true });
  let screenscraperConfig = $state({ userId: "", userPassword: "" });
  let igdbConfig = $state({ clientId: "", clientSecret: "" });
  let steamgriddbConfig = $state({ apiKey: "", dimensions: "600x900", allowNsfw: false });

  // Verification & connection testing states
  let testingScreenScraper = $state(false);
  let screenScraperTestResult = $state<{ success: boolean; message: string } | null>(null);

  let testingIgdb = $state(false);
  let igdbTestResult = $state<{ success: boolean; message: string } | null>(null);

  let testingSteamGrid = $state(false);
  let steamGridTestResult = $state<{ success: boolean; message: string } | null>(null);



  async function loadFeatureConfigs() {
    try {
      const [trailers, ambient, gamerpower, storage, screenscraper, igdb, steamgriddb] = await Promise.all([
        pluginEngine.loadPluginConfig("media-trailers"),
        pluginEngine.loadPluginConfig("media-ambient"),
        pluginEngine.loadPluginConfig("utility-gamerpower"),
        pluginEngine.loadPluginConfig("utility-storage"),
        pluginEngine.loadPluginConfig("metadata-screenscraper"),
        pluginEngine.loadPluginConfig("metadata-igdb"),
        pluginEngine.loadPluginConfig("metadata-steamgriddb")
      ]);

      if (trailers) trailersConfig = { ...trailersConfig, ...trailers };
      if (ambient) ambientConfig = { ...ambientConfig, ...ambient };
      if (gamerpower) gamerpowerConfig = { ...gamerpowerConfig, ...gamerpower };
      if (storage) storageConfig = { ...storageConfig, ...storage };
      if (screenscraper) screenscraperConfig = { ...screenscraperConfig, ...screenscraper };
      if (igdb) igdbConfig = { ...igdbConfig, ...igdb };
      if (steamgriddb) steamgriddbConfig = { ...steamgriddbConfig, ...steamgriddb };
    } catch (e) {
      console.warn("Could not load some feature configurations:", e);
    }
  }

  async function saveFeatureConfigs() {
    await Promise.all([
      pluginEngine.savePluginConfig("media-trailers", trailersConfig),
      pluginEngine.savePluginConfig("media-ambient", ambientConfig),
      pluginEngine.savePluginConfig("utility-gamerpower", gamerpowerConfig),
      pluginEngine.savePluginConfig("utility-storage", storageConfig),
      pluginEngine.savePluginConfig("metadata-screenscraper", screenscraperConfig),
      pluginEngine.savePluginConfig("metadata-igdb", igdbConfig),
      pluginEngine.savePluginConfig("metadata-steamgriddb", steamgriddbConfig)
    ]);
  }

  async function testScreenScraper() {
    testingScreenScraper = true;
    screenScraperTestResult = null;
    try {
      screenScraperTestResult = await pluginEngine.testPluginConnection("metadata-screenscraper", screenscraperConfig);
    } catch (e) {
      screenScraperTestResult = { success: false, message: errorMessage(e) };
    } finally {
      testingScreenScraper = false;
    }
  }

  async function testIgdb() {
    testingIgdb = true;
    igdbTestResult = null;
    try {
      igdbTestResult = await pluginEngine.testPluginConnection("metadata-igdb", igdbConfig);
    } catch (e) {
      igdbTestResult = { success: false, message: errorMessage(e) };
    } finally {
      testingIgdb = false;
    }
  }

  async function testSteamGrid() {
    testingSteamGrid = true;
    steamGridTestResult = null;
    try {
      steamGridTestResult = await pluginEngine.testPluginConnection("metadata-steamgriddb", steamgriddbConfig);
    } catch (e) {
      steamGridTestResult = { success: false, message: errorMessage(e) };
    } finally {
      testingSteamGrid = false;
    }
  }

  async function refreshAllMetadata() {
    syncingLibraryMeta = true;
    syncLibraryMsg = null;
    try {
      const res = await api.syncLibraryMetadata(form?.accountId3 ?? undefined, true, syncLibraryAutoArtwork);
      syncLibraryMsg = `Updated ${res.fetched} games (${res.cached} cached, ${res.unavailable} unresolved).`;
      cacheStats = await api.getCacheStats().catch(() => null);
    } catch (e) {
      syncLibraryMsg = errorMessage(e);
    } finally {
      syncingLibraryMeta = false;
    }
  }

  async function handleClearCache() {
    clearingCache = true;
    clearCacheMsg = null;
    try {
      const res = await api.clearMetadataCache(form?.accountId3 ?? undefined, true);
      clearCacheMsg = `Cache reset: ${res.totalMetadataRecords} metadata records re-indexed. ${res.message}`;
      cacheStats = await api.getCacheStats().catch(() => null);
      showClearCacheConfirm = false;
    } catch (e) {
      clearCacheMsg = errorMessage(e);
    } finally {
      clearingCache = false;
    }
  }

  /** Non-Steam sources surfaced in Settings (mirrors the backend platform ids). */
  const platformList: { id: string; label: string }[] = [
    { id: "gog", label: "GOG" },
    { id: "epic", label: "Epic Games" },
    { id: "itch", label: "itch.io" },
    { id: "ubisoft", label: "Ubisoft Connect" },
    { id: "ea", label: "EA" },
    { id: "battlenet", label: "Battle.net" },
    { id: "amazon", label: "Amazon Games" },
    { id: "rockstar", label: "Rockstar" },
    { id: "xbox", label: "Xbox / Microsoft Store" },
    { id: "other", label: "Other studios" }
  ];

  type SourceRow = { id: string; label: string; enabled: boolean; roots: string[] };
  let sources = $state<SourceRow[]>([]);
  let finding = $state<string | null>(null);

  /** Project saved preferences onto the fixed platform list. */
  function buildSources() {
    const savedById = new Map(
      (form?.platformSources ?? []).map((source) => [source.id, source])
    );
    sources = platformList.map((platform) => {
      const entry = savedById.get(platform.id);
      return {
        id: platform.id,
        label: platform.label,
        enabled: entry?.enabled ?? true,
        roots: [...(entry?.roots ?? [])]
      };
    });
  }

  /** Add another (empty) library folder input for a platform. */
  function addRoot(source: SourceRow) {
    source.roots = [...source.roots, ""];
    sources = [...sources];
  }

  /** Remove one library folder input. */
  function removeRoot(source: SourceRow, index: number) {
    source.roots = source.roots.filter((_, i) => i !== index);
    sources = [...sources];
  }

  /** Update one library folder input. */
  function setRoot(source: SourceRow, index: number, value: string) {
    source.roots = source.roots.map((root, i) => (i === index ? value : root));
    sources = [...sources];
  }

  /** Autofill a platform's detected library folders. */
  async function findFolders(source: SourceRow) {
    finding = source.id;
    try {
      const found = await api.findPlatformFolders(source.id);
      const roots = source.roots.filter((root) => root.trim());
      for (const folder of found) {
        if (!roots.includes(folder)) roots.push(folder);
      }
      source.roots = roots;
      source.enabled = true;
      sources = [...sources];
      error.set(null);
    } catch (e) {
      error.set(errorMessage(e));
    } finally {
      finding = null;
    }
  }

  function setTab(tab: SettingsTab) {
    activeTab = tab;
    const url = new URL(window.location.href);
    url.searchParams.set("tab", tab);
    goto(url.pathname + url.search, { replaceState: true, noScroll: true, keepFocus: true });
  }

  onMount(async () => {
    try {
      const params = new URLSearchParams(window.location.search);
      let tabParam = params.get("tab") as string | null;
      if (tabParam === "platforms") tabParam = "providers";
      if (tabParam && ["general", "providers", "api", "media", "storage"].includes(tabParam)) {
        activeTab = tabParam as SettingsTab;
      }

      await loadFeatureConfigs();

      const [loaded, detected, stats] = await Promise.all([
        api.getSettings(),
        api.detectSteam(),
        api.getCacheStats().catch(() => null)
      ]);
      cacheStats = stats;
      settings.set(loaded);
      form = { ...loaded };
      if (loaded.language) {
        syncWithSettingsLanguage(loaded.language);
      }
      install.set(detected);
      if (!form.steamRoot && detected) form.steamRoot = detected.root;
      if (!form.accountId3 && detected?.accounts[0]) form.accountId3 = detected.accounts[0].id3;
      buildSources();
    } catch (e) {
      error.set(errorMessage(e));
    }
  });

  async function detect() {
    try {
      const detected = await api.detectSteam();
      install.set(detected);
      if (detected && form) {
        form.steamRoot = detected.root;
        form.accountId3 ??= detected.accounts[0]?.id3 ?? null;
      }
      error.set(null);
    } catch (e) {
      error.set(errorMessage(e));
    }
  }

  async function save() {
    if (!form) return;
    form.platformSources = sources.map((source) => ({
      id: source.id,
      enabled: source.enabled,
      roots: source.roots.map((value) => value.trim()).filter(Boolean)
    }));
    busy = true;
    try {
      const [updated] = await Promise.all([
        api.saveSettings(form),
        saveFeatureConfigs()
      ]);
      settings.set(updated);
      form = { ...updated };
      if (updated.language) {
        setLocale(updated.language);
      }
      saved = true;
      setTimeout(() => (saved = false), 2200);
      error.set(null);
    } catch (e) {
      error.set(errorMessage(e));
    } finally {
      busy = false;
    }
  }
</script>

<div class="row-between" style="margin-bottom:1rem">
  <div>
    <h1>{$t("settings.title")}</h1>
    <p class="muted">Manage launcher preferences, providers, external APIs, and local storage.</p>
  </div>
  <div class="row" style="align-items:center;gap:0.75rem">
    {#if saved}<span class="pill pill-ok">{$t("common.saved")}</span>{/if}
    <button class="btn btn-primary" disabled={busy || !form} onclick={save}>
      {busy ? $t("common.saving") : $t("common.save")}
    </button>
  </div>
</div>

<!-- Section Navigation Tabs: Organized by Type -->
<div class="settings-nav-tabs">
  <button class="tab-btn" class:active={activeTab === "general"} onclick={() => setTab("general")}>
    <span class="tab-icon">⚙️</span>
    <span>General</span>
  </button>
  <button class="tab-btn" class:active={activeTab === "providers"} onclick={() => setTab("providers")}>
    <span class="tab-icon">⬡</span>
    <span>Providers</span>
  </button>
  <button class="tab-btn" class:active={activeTab === "api"} onclick={() => setTab("api")}>
    <span class="tab-icon">🔑</span>
    <span>API &amp; Credentials</span>
  </button>
  <button class="tab-btn" class:active={activeTab === "media"} onclick={() => setTab("media")}>
    <span class="tab-icon">🎬</span>
    <span>Media &amp; Audio</span>
  </button>
  <button class="tab-btn" class:active={activeTab === "storage"} onclick={() => setTab("storage")}>
    <span class="tab-icon">💾</span>
    <span>Storage &amp; Cache</span>
  </button>
</div>

{#if form}
  <!-- TYPE 1: GENERAL SETTINGS -->
  {#if activeTab === "general"}
    <div class="settings-columns">
      <div class="settings-col">
        <!-- Steam Folder & Active Account -->
        <section class="glass-strong card">
          <div class="row-between" style="align-items:center;margin-bottom:0.75rem">
            <h2 class="section-title" style="margin:0">{$t("settings.steamFolder")}</h2>
            <button class="btn mini" onclick={detect}>{$t("settings.autoDetect")}</button>
          </div>
          <div class="field">
            <label for="root">Steam Installation Directory</label>
            <input id="root" bind:value={form.steamRoot} placeholder="C:\Program Files (x86)\Steam" />
          </div>
          <div class="field">
            <label for="account">{$t("settings.activeAccount")}</label>
            <select id="account" bind:value={form.accountId3}>
              <option value={null}>Automatic (first found)</option>
              {#each $install?.accounts ?? [] as account (account.id3)}
                <option value={account.id3}>
                  {(account.personaName ?? account.accountName) || "Unknown"} ({account.id3})
                </option>
              {/each}
            </select>
          </div>
          <div class="field" style="margin-top:0.5rem">
            <label for="cc">Store Country Code</label>
            <input id="cc" bind:value={form.countryCode} placeholder="us" />
          </div>
          {#if $install}
            <div class="kv" style="margin-top:0.5rem"><span class="muted">{$t("settings.librariesCount")}</span><span>{$install.libraries.length} folders</span></div>
            <div class="kv"><span class="muted">{$t("settings.accountsCount")}</span><span>{$install.accounts.length} accounts</span></div>
          {/if}
        </section>
      </div>

      <div class="settings-col">
        <!-- Notifications & Feeds -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Notifications &amp; Alerts</h2>
          <p class="muted-xs" style="margin-bottom:1rem">
            Configure desktop notifications and feed updates for new deals and promotions.
          </p>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-bottom: 0.85rem; cursor: pointer;">
            <input type="checkbox" bind:checked={gamerpowerConfig.enableFreeGames} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">100% Free Game Alerts</strong>
              <span class="muted-xs">Monitor free game giveaways across Epic, Steam, GOG, and itch.io.</span>
            </div>
          </label>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-bottom: 0.85rem; cursor: pointer;">
            <input type="checkbox" bind:checked={gamerpowerConfig.enableDeals} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Store Deals &amp; Discounts</strong>
              <span class="muted-xs">Surface highlighted promotions and major price cuts in the store view.</span>
            </div>
          </label>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-bottom: 0.85rem; cursor: pointer;">
            <input type="checkbox" bind:checked={gamerpowerConfig.notifyDesktop} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Desktop Push Notifications</strong>
              <span class="muted-xs">Display native OS notifications when high-value free titles go live.</span>
            </div>
          </label>
        </section>
      </div>
    </div>

  <!-- TYPE 2: PROVIDERS -->
  {:else if activeTab === "providers"}
    <PlatformsSection
      bind:sources
      bind:freeGameNotifications={form.enableFreeGameNotifications}
      {addRoot}
      {removeRoot}
      {setRoot}
    />

  <!-- TYPE 3: API & CREDENTIALS -->
  {:else if activeTab === "api"}
    <div class="settings-columns">
      <div class="settings-col">
        <!-- ScreenScraper -->
        <section class="glass-strong card">
          <div class="row-between" style="align-items: center; margin-bottom: 0.4rem;">
            <h2 class="section-title">ScreenScraper.fr</h2>
            <a href="https://www.screenscraper.fr" target="_blank" rel="noreferrer" class="muted-xs" style="color: var(--accent);">screenscraper.fr ↗</a>
          </div>
          <p class="muted-xs" style="margin-bottom: 1rem;">
            Keyless retro gaming metadata and artwork provider. Entering your free website username enables faster multi-threaded scraping.
          </p>
          <div class="field">
            <label for="ss-user">ScreenScraper Username</label>
            <input id="ss-user" bind:value={screenscraperConfig.userId} placeholder="Your screenscraper.fr username" />
          </div>
          <div class="field">
            <label for="ss-pass">ScreenScraper Password</label>
            <input id="ss-pass" type="password" bind:value={screenscraperConfig.userPassword} placeholder="Your screenscraper.fr password" />
          </div>
          <div class="row" style="gap: 0.6rem; align-items: center; margin-top: 0.5rem;">
            <button class="btn mini btn-ghost" onclick={testScreenScraper} disabled={testingScreenScraper || !screenscraperConfig.userId}>
              {testingScreenScraper ? "Verifying…" : "Verify Account"}
            </button>
            {#if screenScraperTestResult}
              <span class="muted-xs" style="color: {screenScraperTestResult.success ? '#51cf66' : '#ff8787'};">
                {screenScraperTestResult.message}
              </span>
            {/if}
          </div>
        </section>

        <!-- SteamGridDB -->
        <section class="glass-strong card">
          <div class="row-between" style="align-items: center; margin-bottom: 0.4rem;">
            <h2 class="section-title">SteamGridDB</h2>
            <a href="https://www.steamgriddb.com/profile/preferences/api" target="_blank" rel="noreferrer" class="muted-xs" style="color: var(--accent);">Get API Key ↗</a>
          </div>
          <p class="muted-xs" style="margin-bottom: 1rem;">
            Community custom capsule covers, hero banners, and transparent logos.
          </p>
          <div class="field">
            <label for="sgdb-key">SteamGridDB API Key</label>
            <input id="sgdb-key" type="password" bind:value={steamgriddbConfig.apiKey} placeholder="Paste SteamGridDB API key" />
          </div>
          <div class="field">
            <label for="sgdb-dim">Preferred Capsule Dimensions</label>
            <select id="sgdb-dim" bind:value={steamgriddbConfig.dimensions}>
              <option value="600x900">Vertical Posters (600x900)</option>
              <option value="460x215">Legacy Grids (460x215)</option>
            </select>
          </div>
          <label class="row" style="gap: 0.5rem; align-items: center; margin-bottom: 0.75rem; cursor: pointer;">
            <input type="checkbox" bind:checked={steamgriddbConfig.allowNsfw} style="width: auto;" />
            <span class="muted-sm">Allow Mature / NSFW Submissions</span>
          </label>
          <div class="row" style="gap: 0.6rem; align-items: center;">
            <button class="btn mini btn-ghost" onclick={testSteamGrid} disabled={testingSteamGrid || !steamgriddbConfig.apiKey}>
              {testingSteamGrid ? "Testing…" : "Test SteamGridDB Key"}
            </button>
            {#if steamGridTestResult}
              <span class="muted-xs" style="color: {steamGridTestResult.success ? '#51cf66' : '#ff8787'};">
                {steamGridTestResult.message}
              </span>
            {/if}
          </div>
        </section>
      </div>

      <div class="settings-col">
        <!-- IGDB / Twitch -->
        <section class="glass-strong card">
          <div class="row-between" style="align-items: center; margin-bottom: 0.4rem;">
            <h2 class="section-title">IGDB / Twitch Developer</h2>
            <a href="https://dev.twitch.tv/console/apps" target="_blank" rel="noreferrer" class="muted-xs" style="color: var(--accent);">dev.twitch.tv ↗</a>
          </div>
          <p class="muted-xs" style="margin-bottom: 1rem;">
            Comprehensive taxonomy, developer credits, franchises, and critic ratings via the IGDB API.
          </p>
          <div class="field">
            <label for="igdb-client">Twitch / IGDB Client ID</label>
            <input id="igdb-client" bind:value={igdbConfig.clientId} placeholder="Client ID from Twitch Console" />
          </div>
          <div class="field">
            <label for="igdb-secret">Twitch / IGDB Client Secret</label>
            <input id="igdb-secret" type="password" bind:value={igdbConfig.clientSecret} placeholder="Client Secret" />
          </div>
          <div class="row" style="gap: 0.6rem; align-items: center; margin-top: 0.5rem;">
            <button class="btn mini btn-ghost" onclick={testIgdb} disabled={testingIgdb || !igdbConfig.clientId || !igdbConfig.clientSecret}>
              {testingIgdb ? "Testing…" : "Test IGDB Connection"}
            </button>
            {#if igdbTestResult}
              <span class="muted-xs" style="color: {igdbTestResult.success ? '#51cf66' : '#ff8787'};">
                {igdbTestResult.message}
              </span>
            {/if}
          </div>
        </section>

        <!-- Game Platform APIs & Launchers -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Game Platform APIs & Launchers</h2>
          <p class="muted-xs" style="margin-bottom:1rem">
            API keys and launcher paths for platforms that require them. Launchers are auto-detected; these are only needed for extended metadata and advanced launch support.
          </p>

          <div class="field">
            <label for="steam-web-api">Steam Web API Key</label>
            <input id="steam-web-api" type="password" bind:value={form.steamApiKey} placeholder="Steam Web API key" />
          </div>

          <div class="field">
            <label for="gw2-api">Guild Wars 2 API Key</label>
            <input id="gw2-api" type="password" bind:value={form.gw2ApiKey} placeholder="GW2 API key" />
          </div>

          <div class="field">
            <label for="jagex-path">Jagex Launcher Path</label>
            <input id="jagex-path" bind:value={form.jagexLauncherPath} placeholder="Path to Jagex Launcher executable" />
          </div>

          <div class="field">
            <label for="xiv-path">XIV Launcher Path</label>
            <input id="xiv-path" bind:value={form.xivLauncherPath} placeholder="Path to XIV Launcher executable" />
          </div>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-top: 0.75rem; cursor: pointer;">
            <input type="checkbox" bind:checked={form.legendaryEnabled} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Use Legendary (bypass Epic Games Launcher)</strong>
              <span class="muted-xs">Prefer the Legendary CLI for Epic titles when available.</span>
            </div>
          </label>
        </section>
      </div>
    </div>

  <!-- TYPE 4: MEDIA & AUDIO -->
  {:else if activeTab === "media"}
    <div class="settings-columns">
      <div class="settings-col">
        <!-- Video Trailers -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Video Trailers</h2>
          <p class="muted-xs" style="margin-bottom:1rem">
            Configure embedded video trailer streaming in the game detail view.
          </p>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-bottom: 0.85rem; cursor: pointer;">
            <input type="checkbox" bind:checked={trailersConfig.autoPlay} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Autoplay Video Trailers</strong>
              <span class="muted-xs">Automatically begin video streaming when opening game details.</span>
            </div>
          </label>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-bottom: 0.85rem; cursor: pointer;">
            <input type="checkbox" bind:checked={trailersConfig.muted} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Mute Trailers by Default</strong>
              <span class="muted-xs">Keep game trailers muted until unmuted manually.</span>
            </div>
          </label>
        </section>
      </div>

      <div class="settings-col">
        <!-- Audio & Soundtracks -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Ambient Soundtracks</h2>
          <p class="muted-xs" style="margin-bottom:1rem">
            Configure background soundtrack themes and audio playback.
          </p>

          <div style="margin-bottom: 1rem;">
            <div class="row-between" style="align-items: center; margin-bottom: 0.35rem;">
              <strong style="font-size: 0.88rem;">Master Soundtrack Volume</strong>
              <span class="pill pill-dim" style="font-size: 0.72rem;">{ambientConfig.volume}%</span>
            </div>
            <input
              type="range"
              min="0"
              max="100"
              bind:value={ambientConfig.volume}
              style="width: 100%; cursor: pointer;"
            />
          </div>

          <label class="row" style="gap: 0.6rem; align-items: center; cursor: pointer;">
            <input type="checkbox" bind:checked={ambientConfig.loopPlayback} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Loop Soundtrack Themes</strong>
              <span class="muted-xs">Seamlessly repeat audio tracks while browsing title details.</span>
            </div>
          </label>
        </section>
      </div>
    </div>

  <!-- TYPE 5: STORAGE & CACHE -->
  {:else if activeTab === "storage"}
    <div class="settings-columns">
      <div class="settings-col">
        <!-- Preferences & Offline Cache -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Local Fast-Tier Storage Architecture</h2>
          <p class="muted-sm" style="margin-bottom:1rem">
            Universal Game Launcher operates completely non-invasively. Native launcher files (Steam, Epic, GOG) are never modified. All collections, tags, custom art, and launcher preferences are safely persisted in your local fast-tier SQLite database and in-memory Redis KV cache.
          </p>
          <div class="kv">
            <div>
              <strong>In-Memory Redis KV Entries</strong>
              <p class="muted-xs" style="margin:0">Sub-millisecond fast-path cache keys.</p>
            </div>
            <span>{cacheStats?.totalKvEntries ?? 0} keys</span>
          </div>
          <div class="kv">
            <div>
              <strong>Cached Metadata Records</strong>
              <p class="muted-xs" style="margin:0">Locally stored SQLite metadata records.</p>
            </div>
            <span>{cacheStats?.totalMetadataCached ?? 0} records</span>
          </div>
          <div class="kv">
            <div>
              <strong>Cached Artwork Assets</strong>
              <p class="muted-xs" style="margin:0">Persistent offline grid &amp; hero artwork covers.</p>
            </div>
            <span>{cacheStats?.totalCachedArtworks ?? 0} files</span>
          </div>

          <label class="row" style="gap: 0.6rem; align-items: center; margin-top: 1rem; padding-top: 1rem; border-top: 1px solid rgba(255,255,255,0.06); cursor: pointer;">
            <input type="checkbox" bind:checked={storageConfig.backgroundSync} style="width: auto;" />
            <div>
              <strong style="font-size: 0.88rem; display: block;">Background Storage Analyzer</strong>
              <span class="muted-xs">Inspect disk space and folder sizes asynchronously without UI hitches.</span>
            </div>
          </label>
        </section>
      </div>

      <div class="settings-col">
        <!-- Cache Maintenance Operations -->
        <section class="glass-strong card">
          <h2 class="section-title" style="margin-bottom:0.5rem">Cache Maintenance</h2>

          <div>
            <div class="row-between" style="align-items: center; margin-bottom: 0.35rem;">
              <strong style="font-size: 0.95rem;">{$t("settings.refreshMetadata")}</strong>
              {#if syncingLibraryMeta}
                <span class="pill pill-warn">{$t("settings.refreshMetadataRunning")}</span>
              {/if}
            </div>
            <p class="muted-xs" style="margin-bottom: 0.75rem;">
              {$t("settings.refreshMetadataHelp")}
            </p>
            <label class="row" style="gap: 0.5rem; margin-bottom: 0.75rem; cursor: pointer;">
              <input type="checkbox" bind:checked={syncLibraryAutoArtwork} style="width: auto;" />
              <span class="muted-sm">{$t("settings.alsoFetchArtwork")}</span>
            </label>
            <div class="row" style="gap: 0.75rem; align-items: center;">
              <button class="btn mini primary" onclick={refreshAllMetadata} disabled={syncingLibraryMeta}>
                {syncingLibraryMeta ? $t("settings.refreshMetadataRunning") : $t("settings.refreshMetadata")}
              </button>
              {#if syncLibraryMsg}
                <span class="muted-sm">{syncLibraryMsg}</span>
              {/if}
            </div>
          </div>

          <div style="margin-top: 1.25rem; padding-top: 1rem; border-top: 1px solid rgba(255, 255, 255, 0.08);">
            <div class="row-between" style="align-items: center; margin-bottom: 0.35rem;">
              <strong style="font-size: 0.95rem; color: #ff6b6b;">Clear &amp; Reset Metadata Cache</strong>
              {#if clearingCache}
                <span class="pill pill-warn">Purging &amp; Resetting…</span>
              {/if}
            </div>
            <p class="muted-xs" style="margin-bottom: 0.75rem;">
              Purges in-memory Redis KV entries, stored artwork, and cached metadata records if stale or corrupted. Clean metadata and artwork will be re-fetched as you browse.
            </p>
            {#if !showClearCacheConfirm}
              <div class="row" style="gap: 0.75rem; align-items: center;">
                <button class="btn mini" style="border-color: rgba(255,107,107,0.4); color: #ff8585;" onclick={() => (showClearCacheConfirm = true)} disabled={clearingCache}>
                  Purge &amp; Reset All Caches…
                </button>
                {#if clearCacheMsg}
                  <span class="muted-sm">{clearCacheMsg}</span>
                {/if}
              </div>
            {:else}
              <div style="padding: 0.75rem 1rem; background: rgba(255, 107, 107, 0.1); border: 1px solid rgba(255, 107, 107, 0.3); border-radius: 8px;">
                <p style="font-size: 0.85rem; margin: 0 0 0.75rem 0; color: #ffc9c9;">
                  Are you sure? This will delete all downloaded artwork covers, cached metadata, and in-memory KV records. Clean metadata and artwork will be re-fetched as you browse.
                </p>
                <div class="row" style="gap: 0.5rem; align-items: center;">
                  <button class="btn mini" style="background: #e03131; border-color: #c92a2a; color: white;" onclick={handleClearCache} disabled={clearingCache}>
                    {clearingCache ? "Purging…" : "Yes, Purge and Reset"}
                  </button>
                  <button class="btn mini" onclick={() => (showClearCacheConfirm = false)} disabled={clearingCache}>
                    Cancel
                  </button>
                </div>
              </div>
            {/if}
          </div>
        </section>
      </div>
    </div>
  {/if}
{:else}
  <p class="muted">Loading settings…</p>
{/if}
