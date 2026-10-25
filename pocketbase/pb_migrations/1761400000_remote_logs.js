/// <reference path="../pb_data/types.d.ts" />

migrate((app) => {
  const uploaders = new Collection({ type: "auth", name: "uploaders" })
  uploaders.authToken.duration = 1800
  uploaders.authRule = null
  app.save(uploaders)

  const readers = new Collection({ type: "auth", name: "readers" })
  readers.authToken.duration = 1800
  readers.authRule = null
  app.save(readers)

  app.save(new Collection({
    type: "base",
    name: "authorized_keys",
    fields: [
      { type: "text", name: "name", required: true },
      { type: "text", name: "public_key", required: true },
      { type: "bool", name: "enabled" },
    ],
  }))

  app.save(new Collection({
    type: "base",
    name: "logs",
    listRule: '@request.auth.collectionName = "readers"',
    viewRule: '@request.auth.collectionName = "readers"',
    createRule: '@request.auth.collectionName = "uploaders"',
    fields: [
      { type: "text", name: "host", required: true },
      { type: "text", name: "user", required: true },
      { type: "date", name: "started", required: true },
      { type: "number", name: "duration_s" },
      { type: "number", name: "size" },
      { type: "file", name: "file", required: true, maxSelect: 1, maxSize: 53687091200, protected: true },
    ],
  }))

  for (const [collection, email] of [[uploaders, "uploader@kenzo.invalid"], [readers, "reader@kenzo.invalid"]]) {
    const record = new Record(collection)
    record.set("email", email)
    record.setPassword($security.randomString(40))
    app.save(record)
  }
}, (app) => {
  for (const name of ["logs", "authorized_keys", "readers", "uploaders"]) {
    app.delete(app.findCollectionByNameOrId(name))
  }
})
