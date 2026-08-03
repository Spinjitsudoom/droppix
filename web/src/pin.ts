/** Pairing-PIN helpers. The web client validates the typed code locally against
 *  the pairingCode the host serves in config.json — a "right PC" check, not auth. */
export function normalizePin(raw: string): string {
  return raw.replace(/\D/g, "").slice(0, 6);
}
export function pinComplete(code: string): boolean {
  return /^\d{6}$/.test(code);
}
export function pinMatches(entered: string, served: string): boolean {
  return normalizePin(entered) === normalizePin(served);
}
