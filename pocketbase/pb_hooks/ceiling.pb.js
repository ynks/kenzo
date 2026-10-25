/// <reference path="../pb_data/types.d.ts" />

onRecordCreateRequest((e) => {
  const DEFAULT_MAX_TOTAL_BYTES = 200 * 1024 * 1024 * 1024
  const configured = Number($os.getenv("KENZO_MAX_TOTAL_BYTES"))
  const max = configured > 0 ? configured : DEFAULT_MAX_TOTAL_BYTES

  const row = new DynamicModel({ total: 0 })
  $app.db().newQuery("SELECT COALESCE(SUM(size), 0) AS total FROM logs").one(row)

  if (row.total + e.record.getInt("size") > max) {
    throw new BadRequestError("storage full")
  }
  e.next()
}, "logs")
