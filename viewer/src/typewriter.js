// Typing out the inspector: every character of the explanation gets an offset, and a single "revealed" counter (that
// grows over time) decides what is typed so far. Text not typed yet keeps its place as an invisible ghost, so the layout
// never jumps; values not typed yet show as gaps that fill in when the typing reaches them.

// lines: arrays of tokens (strings are prose, { v } are values). Returns the lines with each token's text, kind and
// [start, end) character range, and the total number of characters.
export function typePlan(lines) {
  let at = 0
  const planned = lines.map(line => (line ?? []).map(token => {
    const text = typeof token === 'string' ? token : String(token.v ?? '')
    const t = { text, value: typeof token !== 'string', tone: typeof token === 'string' ? null : token.tone ?? null, start: at, end: at + text.length }
    at += text.length
    return t
  }))
  return { lines: planned, total: at }
}

// What of a token is typed at `revealed` characters: the shown text, the ghost rest, and whether the caret is in it.
export function typedParts(token, revealed) {
  const n = Math.max(0, Math.min(token.text.length, Math.floor(revealed) - token.start))
  return { shown: token.text.slice(0, n), ghost: token.text.slice(n), done: n === token.text.length, active: n > 0 && n < token.text.length }
}

// Where a line starts and ends in characters (graphics and headings appear when the typing reaches them).
export const lineStart = line => (line.length ? line[0].start : Infinity)
export const lineEnd = line => (line.length ? line.at(-1).end : -Infinity)

// Typing speed: fast enough that a long explanation takes about two seconds, never slower than a brisk typist.
export function typingRate(total, { seconds = 1.9, minimum = 90 } = {}) {
  return Math.max(minimum, total / seconds)
}
