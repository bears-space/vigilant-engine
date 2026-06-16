declare const __VIGILANT_ENGINE_GIT_HASH__: string | undefined;

const rawGitHash =
  typeof __VIGILANT_ENGINE_GIT_HASH__ === "string"
    ? __VIGILANT_ENGINE_GIT_HASH__
    : "unknown";

export const buildGitHash = rawGitHash;
export const buildGitHashShort =
  rawGitHash === "unknown" ? "unknown" : rawGitHash.slice(0, 12);
export const buildGitHashTitle =
  rawGitHash === "unknown" ? "Build commit unavailable" : `Build commit ${rawGitHash}`;
