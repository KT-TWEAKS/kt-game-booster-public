import test from 'node:test';
import assert from 'node:assert/strict';
import { shouldAnimate } from './kt-atmosphere.js';

test('anima a atmosfera somente quando o visitante não pediu menos movimento', () => {
  assert.equal(shouldAnimate({ reducedMotion: false, hidden: false, focusMode: false }), true);
  assert.equal(shouldAnimate({ reducedMotion: true, hidden: false, focusMode: false }), false);
  assert.equal(shouldAnimate({ reducedMotion: false, hidden: true, focusMode: false }), false);
  assert.equal(shouldAnimate({ reducedMotion: false, hidden: false, focusMode: true }), false);
});
