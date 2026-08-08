/*
 * Fruit Chop -- phone side.
 *
 * The game itself is entirely on the watch; this exists only so a score can
 * leave it. There is no network call here and no server behind it.
 *
 * A watchapp cannot put anything on the phone screen by itself: the only hook
 * is `showConfiguration`, which fires when the player taps the settings gear
 * next to Fruit Chop in the Pebble app. So the run result is stored as it
 * arrives and the share card is opened later, whenever the player goes looking
 * for it.
 *
 * ES5 only -- this runs in the Pebble JS runtime, not a modern browser.
 */

var SHARE_URL = 'https://qichuan.github.io/pebble-fruit-chop/';

var DIFF_NAMES = ['EASY', 'NORMAL', 'HARD'];

function diffName(d) {
  return DIFF_NAMES[d] || DIFF_NAMES[1];
}

function loadRun() {
  try {
    return JSON.parse(localStorage.getItem('last_run') || 'null');
  } catch (e) {
    return null;
  }
}

Pebble.addEventListener('ready', function () {
  console.log('fruit-chop: pkjs ready');
});

// The watch sends exactly one of these, on the frame a run ends.
Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.SCORE === undefined) {
    return;
  }

  var run = {
    score: p.SCORE | 0,
    diff: p.DIFF | 0,
    best: p.BEST | 0,
    // Seconds, so the card can say how old the score is. The player may not
    // open the gear until much later, or ever.
    ts: Math.floor(Date.now() / 1000)
  };
  localStorage.setItem('last_run', JSON.stringify(run));
  console.log('fruit-chop: run ended, score=' + run.score +
              ' diff=' + diffName(run.diff) + ' best=' + run.best);
});

Pebble.addEventListener('showConfiguration', function () {
  var run = loadRun();
  var url = SHARE_URL + '?v=1';
  if (run) {
    url += '&score=' + run.score +
           '&diff=' + encodeURIComponent(diffName(run.diff)) +
           '&best=' + run.best +
           '&ts=' + run.ts;
  }
  // The query string is the whole contract with the page: nothing else crosses.
  Pebble.openURL(url);
});

// Nothing to save -- the page is a share card, not a settings form. The handler
// still has to exist so the webview closes cleanly.
Pebble.addEventListener('webviewclosed', function () {
  console.log('fruit-chop: share card closed');
});
