/// <reference path="../pb_data/types.d.ts" />

const CHALLENGE_LIFETIME_MS = 2 * 60 * 1000
const PUBLIC_KEY = /^ssh-ed25519 [A-Za-z0-9+\/]+=*$/

function now() {
  let offset = 0
  const clockFile = $os.getenv("KENZO_TEST_CLOCK_FILE")
  if (clockFile) {
    try {
      offset = Number(toString($os.readFile(clockFile)).trim()) || 0
    } catch (_) {}
  }
  return Date.now() + offset
}

function secret() {
  let value = $app.store().get("kenzoChallengeSecret")
  if (!value) {
    value = $security.randomString(48)
    $app.store().set("kenzoChallengeSecret", value)
  }
  return value
}

function makeChallenge() {
  const stamp = String(now())
  return stamp + "." + $security.hs256(stamp, secret())
}

function checkChallenge(challenge) {
  if (typeof challenge !== "string") {
    return false
  }
  const dot = challenge.indexOf(".")
  if (dot < 1) {
    return false
  }
  const stamp = challenge.slice(0, dot)
  if (!/^[0-9]{1,15}$/.test(stamp)) {
    return false
  }
  if (!$security.equal(challenge.slice(dot + 1), $security.hs256(stamp, secret()))) {
    return false
  }
  const age = now() - Number(stamp)
  return age >= 0 && age <= CHALLENGE_LIFETIME_MS
}

function verifySignature(challenge, signature, keyLines) {
  const entries = []
  for (const line of keyLines) {
    if (typeof line !== "string" || /[\r\n]/.test(line.trim())) {
      continue
    }
    const key = line.trim().split(/\s+/).slice(0, 2).join(" ")
    if (PUBLIC_KEY.test(key)) {
      entries.push(`kenzo namespaces="kenzo" ${key}`)
    }
  }
  if (entries.length === 0) {
    return false
  }

  const dir = `${$os.tempDir()}/kenzo-auth-${$security.randomString(16)}`
  $os.mkdirAll(dir, 0o700)
  try {
    $os.writeFile(`${dir}/allowed`, entries.join("\n") + "\n", 0o600)
    $os.writeFile(`${dir}/sig`, signature, 0o600)
    $os.writeFile(`${dir}/msg`, challenge, 0o600)
    const command = $os.cmd(
      "sh",
      "-c",
      'ssh-keygen -Y verify -f "$1" -I kenzo -n kenzo -s "$2" < "$3"',
      "sh",
      `${dir}/allowed`,
      `${dir}/sig`,
      `${dir}/msg`,
    )
    try {
      command.output()
      return true
    } catch (_) {
      return false
    }
  } finally {
    $os.removeAll(dir)
  }
}

function consumeChallenge(challenge) {
  const at = now()
  let used = {}
  try {
    used = JSON.parse($app.store().get("kenzoUsedChallenges") || "{}")
  } catch (_) {}
  for (const key of Object.keys(used)) {
    if (used[key] < at) {
      delete used[key]
    }
  }
  if (used[challenge]) {
    return false
  }
  used[challenge] = at + CHALLENGE_LIFETIME_MS + 1000
  $app.store().set("kenzoUsedChallenges", JSON.stringify(used))
  return true
}

module.exports = { now, makeChallenge, checkChallenge, verifySignature, consumeChallenge }
