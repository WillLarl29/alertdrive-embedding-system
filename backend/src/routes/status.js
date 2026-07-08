const express = require("express");
const systemState = require("../state/systemState");

const router = express.Router();

router.get("/status", (req, res) => {
  res.json(systemState.getState());
});

module.exports = router;
