const express = require("express");
const alertStore = require("../services/alertStore");

const router = express.Router();

router.get("/alerts", (req, res) => {
  const n = Number(req.query.limit) || 50;
  res.json(alertStore.getRecent(n));
});

module.exports = router;
