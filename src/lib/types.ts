// Shared TypeScript models mirroring the Rust backend (`src-tauri/src/steam/models.rs`).

export type GameKind = "steam" | "shortcut";

/** A storefront / launcher a game can come from (dynamically contributed by platform plugins). */
export type Platform = string;

export interface SteamSearchResult {
  id: number;
  name: string;
  tinyImage: string | null;
}

export interface SteamLibrary {
  index: number;
  path: string;
  label: string | null;
  appIds: number[];
}

export interface SteamAccount {
  id3: string;
  id64: string | null;
  accountName: string | null;
  personaName: string | null;
}

export interface SteamInstall {
  root: string;
  libraries: SteamLibrary[];
  accounts: SteamAccount[];
}

export interface Game {
  appId: number;
  name: string;
  kind: GameKind;
  libraryPath: string | null;
  installDir: string | null;
  sizeOnDisk: number;
  installed: boolean;
  lastPlayed: number | null;
  playtimeMinutes: number | null;
  collections: string[];
  favorite: boolean;
  hidden: boolean;
  /** Steam for real apps; the source storefront for imported shortcuts. */
  platform: Platform;
  shortcutId: number | null;
  exe: string | null;
  startDir: string | null;
  icon: string | null;
  launchUri?: string | null;
  installUri?: string | null;
  uninstallUri?: string | null;
  platformId?: string | null;
  capsuleImage?: string | null;
  heroImage?: string | null;
  logoImage?: string | null;
  console?: string | null;
}

export interface Collection {
  id: string;
  name: string;
  favorite: boolean;
  hidden: boolean;
  dynamic: boolean;
  added: number[];
  removed: number[];
  filterSpec?: any;
}

export interface FilterPreset {
  id: string;
  name: string;
  filter: Filter;
  createdAt?: number;
}

export interface AppMeta {
  appId: number;
  name: string | null;
  developers: string[];
  publishers: string[];
  genres: string[];
  categories: string[];
  tags: string[];
  releaseDate: string | null;
  releaseYear: number | null;
  shortDescription: string | null;
  headerImage: string | null;
  capsuleImage: string | null;
  heroImage?: string | null;
  logoImage?: string | null;
  iconImage?: string | null;
  userScore: number | null;
  fetchedAt: number | null;
  screenshots?: string[];
  movies?: string[];
  soundtracks?: string[];
  locales?: Record<string, { title?: string; shortDescription?: string | null; aliases?: string[] }>;
  providers?: Record<string, any>;
  userEnhanced?: boolean;
  enhancedBy?: string[];
}

export interface MediaItem {
  type: "video" | "image";
  url: string;
  thumbnailUrl?: string;
  title?: string;
}

export interface ProviderInfo {
  id: string;
  label: string;
  requiresKey: boolean;
  category: string;
  description: string;
}

export interface OwnedGame {
  appid: number;
  name: string | null;
  playtime_forever: number | null;
  playtime_2weeks: number | null;
  img_icon_url: string | null;
}

export interface ScaffoldReport {
  romsDir: string;
  createdFoldersCount: number;
  totalSystemsCount: number;
  scaffoldedSystems: string[];
  message: string;
}


export type PluginType = "platform" | "metadata" | "media" | "utility";

export interface PluginSettingOption {
  label: string;
  value: string;
}

export interface PluginSettingField {
  key: string;
  label: string;
  type: "string" | "text" | "password" | "boolean" | "number" | "select";
  description?: string;
  required?: boolean;
  default?: any;
  placeholder?: string;
  helpUrl?: string;
  helpLabel?: string;
  options?: PluginSettingOption[];
}

export interface PluginSettingsConfig {
  requiresApiKey?: boolean;
  fields?: PluginSettingField[];
}

export interface PluginInfo {
  id: string;
  name: string;
  version: string;
  author?: string | null;
  description?: string | null;
  pluginType: PluginType;
  enabled: boolean;
  builtin: boolean;
  icon?: string | null;
  homepage?: string | null;
  features: string[];
  path: string;
  settings?: PluginSettingsConfig | null;
}

export interface Settings {
  steamRoot: string | null;
  accountId3: string | null;
  steamApiKey: string | null;
  steamgriddbApiKey: string | null;
  igdbClientId: string | null;
  igdbClientSecret: string | null;
  gw2ApiKey: string | null;
  jagexLauncherPath: string | null;
  xivLauncherPath: string | null;
  legendaryEnabled: boolean;
  countryCode: string;
  language: string;
  autoBackup: boolean;
  enabledPlugins?: Record<string, boolean>;
  platformSources: PlatformSource[];
  legendaryPath?: string | null;
  enableFreeGameNotifications?: boolean;
  customToolsDir?: string | null;
  customLaunchersDir?: string | null;
}

export interface ManagedDirectories {
  toolsDir: string;
  launchersDir: string;
  wingetAvailable: boolean;
}

export interface PlatformFriend {
  id: string;
  name: string;
  status: "online" | "in-game" | "offline" | "away" | "busy";
  currentGame?: string | null;
  avatarUrl?: string | null;
}

export interface PlatformAchievement {
  id: string;
  name: string;
  description?: string;
  iconUrl?: string;
  unlocked: boolean;
  unlockedAt?: number | null;
  progress?: number;
  maxProgress?: number;
}

export interface CloudSyncStatus {
  synced: boolean;
  lastSync?: number | null;
  pendingUploads?: number;
  pendingDownloads?: number;
}

/** Per-platform detection preference (mirrors the Rust `PlatformSource`). */
export interface PlatformSource {
  /** Stable platform id: steam, gog, epic, itch, ubisoft, ea, battlenet, … */
  id: string;
  enabled: boolean;
  /** Extra folders to scan for this platform (for example an external drive). */
  roots: string[];
}


// --- External platforms -----------------------------------------------------

/** A game discovered by scanning a non-Steam launcher. */
export interface PlatformGame {
  name: string;
  platform: Platform;
  platformId: string | null;
  installDir: string | null;
  exe: string | null;
  sizeOnDisk: number | null;
  launchUri: string | null;
  source: string | null;
  console?: string | null;
  installed?: boolean;
  installUri?: string | null;
  uninstallUri?: string | null;
}

/** Per-platform scan summary (availability, count, sources consulted). */
export interface PlatformStatus {
  platform: Platform;
  label: string;
  available: boolean;
  count: number;
  roots: string[];
  notes: string | null;
}

export interface PlatformScan {
  games: PlatformGame[];
  platforms: PlatformStatus[];
}

export interface ImportResult {
  added: Game[];
  skipped: string[];
}

// --- Full owned Steam library ----------------------------------------------

export interface OwnedGameRecord {
  appId: number;
  name: string;
  playtimeMinutes: number;
  playtimeTwoWeeks: number;
  iconUrl: string | null;
}

export interface OwnedLibrary {
  fetchedAt: number | null;
  steamId64: string | null;
  games: OwnedGameRecord[];
}

export interface Filter {
  allowed: string[];
  required: string[];
  excluded: string[];
  search: string | null;
  kind: GameKind | null;
}

/** Outcome of warming the local metadata cache. */
export interface MetadataSyncResult {
  requested: number;
  fetched: number;
  tagsFetched: number;
  cached: number;
  unavailable: number;
}

export interface ApplyResult {
  collectionsCreated: number;
  collectionsUpdated: number;
  gamesAssigned: number;
}

// --- Storage management -----------------------------------------------------

/** One registered Steam library folder with its size and drive capacity. */
export interface LibraryEntry {
  index: number;
  path: string;
  label: string | null;
  exists: boolean;
  isSteamRoot: boolean;
  gameCount: number;
  totalBytes: number;
  diskTotal: number | null;
  diskFree: number | null;
}

/** An installed Steam app or external game shortcut and the library folder that holds it. */
export interface GamePlacement {
  appId: number;
  name: string;
  libraryPath: string;
  installDir: string | null;
  sizeOnDisk: number;
  installed: boolean;
  folderExists: boolean;
  platform?: Platform | null;
}

/** A mounted disk, for the drive overview. */
export interface DiskInfo {
  name: string;
  mount: string;
  total: number;
  free: number;
  removable: boolean;
}

/** Everything the Storage view needs in one round-trip. */
export interface StorageReport {
  root: string;
  libraries: LibraryEntry[];
  games: GamePlacement[];
  disks: DiskInfo[];
}

/** A dry-run summary of what a move would do. */
export interface MovePreview {
  from: string;
  to: string;
  bytes: number;
  targetFree: number | null;
  /** Whether the destination has room for the content plus headroom. */
  fits: boolean;
  /** True when the move is on one volume and will therefore be instant. */
  sameVolume: boolean;
  warnings: string[];
}

/** The result of a completed move. */
export interface MoveOutcome {
  from: string;
  to: string;
  bytes: number;
  files: number;
  sameVolume: boolean;
  /** `libraryfolders.vdf` files that were updated. */
  vdfUpdated: string[];
}

/** A throttled `storage-progress` event emitted during a long copy. */
export interface StorageProgress {
  label: string;
  done: number;
  total: number;
  files: number;
}

// --- Custom artwork ---------------------------------------------------------

/** One of Steam's five custom-artwork slots. */
export type ArtworkSlotId = "gridPortrait" | "gridLandscape" | "hero" | "logo" | "icon";

/** The on-disk state of one artwork slot for a game. */
export interface ArtworkSlotState {
  slot: ArtworkSlotId;
  label: string;
  present: boolean;
  path: string | null;
  bytes: number;
  modified: number | null;
}

/** The artwork state for one game across all five slots. */
export interface GameArtwork {
  appId: number;
  name: string;
  slots: ArtworkSlotState[];
  /** How many of the five slots are currently filled. */
  applied: number;
}

/** The result of applying or removing a slot's artwork. */
export interface ArtworkOutcome {
  appId: number;
  slot: ArtworkSlotId;
  /** `"applied"` or `"removed"`. */
  action: string;
  path: string | null;
  bytes: number;
  source: string | null;
  /** Where the replaced image was archived, when there was one. */
  backup: string | null;
}

// --- Persistent cache statistics --------------------------------------------

export interface CacheStats {
  totalMetadataCached: number;
  totalCachedArtworks: number;
  totalKvEntries: number;
}

export interface CacheResetReport {
  totalMetadataRecords: number;
  message: string;
}

export interface LauncherStatus {
  id: string;
  name: string;
  installed: boolean;
  executablePath?: string | null;
  alternativeMethod?: string | null;
  downloadUrl: string;
  alternativeUrl?: string | null;
  alternativeLabel?: string | null;
  supportsSilentInstall?: boolean;
  wingetPackageId?: string | null;
  launchArgs?: string | null;
}

export interface LegendaryAuthStatus {
  isInstalled: boolean;
  isLoggedIn: boolean;
  accountName?: string | null;
  accountId?: string | null;
  authUrl: string;
  directAuthUrl?: string;
  executablePath?: string | null;
}

export interface EosOverlayStatus {
  isInstalled: boolean;
  isEnabled: boolean;
  version?: string | null;
  path?: string | null;
}

export interface FreeGamePromotion {
  id: string;
  title: string;
  platform: string;
  description?: string | null;
  url: string;
  imageUrl?: string | null;
  startDate?: string | null;
  endDate?: string | null;
  isActive: boolean;
  worth?: string | null;
}

export interface StoreDeal {
  id: string;
  title: string;
  platform: string;
  url: string;
  imageUrl?: string | null;
  discountPercent: number;
  originalPrice?: string | null;
  finalPrice: string;
  endDate?: string | null;
}

export interface FeaturedStoreItem {
  id: string;
  title: string;
  platform: string;
  storeUrl: string;
  mainImage: string;
  thumbnails: string[];
  description: string;
  tags: string[];
  originalPrice?: string | null;
  finalPrice: string;
  discountPercent?: number | null;
  isFree: boolean;
  tagLabel?: string | null;
}

export interface PatchNote {
  id: string;
  appId: string;
  gameName: string;
  platform: string;
  title: string;
  url: string;
  date: string;
  author?: string | null;
  summary?: string | null;
}

export interface DownloadItem {
  id: string;
  name: string;
  category: "tool" | "overlay" | "artwork" | "metadata" | "game";
  progress: number;
  speed?: string;
  status: "queued" | "downloading" | "completed" | "failed";
  error?: string;
}


