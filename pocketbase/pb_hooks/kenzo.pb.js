/// <reference path="../pb_data/types.d.ts" />

routerAdd("GET", "/kenzo/token", (e) => {
  const uploader = $app.findFirstRecordByFilter("uploaders", "id != ''")
  return e.json(200, { token: uploader.newAuthToken() })
})

routerAdd("GET", "/kenzo/challenge", (e) => {
  const lib = require(`${__hooks}/kenzo_lib.js`)
  return e.json(200, { challenge: lib.makeChallenge() })
})

routerAdd("POST", "/kenzo/auth", (e) => {
  const lib = require(`${__hooks}/kenzo_lib.js`)

  const body = e.requestInfo().body
  const challenge = body.challenge
  const signature = body.signature
  if (typeof challenge !== "string" || typeof signature !== "string") {
    return e.json(400, { error: "challenge and signature are required" })
  }
  if (!lib.checkChallenge(challenge)) {
    return e.json(401, { error: "challenge expired or invalid" })
  }

  const rows = $app.findRecordsByFilter("authorized_keys", "enabled = true", "", 500, 0)
  const keys = rows.map((row) => row.getString("public_key"))
  if (!lib.verifySignature(challenge, signature, keys)) {
    return e.json(401, { error: "key not whitelisted" })
  }
  if (!lib.consumeChallenge(challenge)) {
    return e.json(401, { error: "challenge already used" })
  }

  const reader = $app.findFirstRecordByFilter("readers", "id != ''")
  return e.json(200, { token: reader.newAuthToken() })
})
