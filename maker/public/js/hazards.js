// The game's deadly blocks, by setting (theme): spikes, lava, burning
// stakes, thorns, a deadly pool. The game kills Alex on any block of class
// $60 (engine/src/game/alex/tiles.c); these are the ones each setting draws,
// offered as whole pieces (a top over a body). Numbers are the setting's own
// metatiles, so each piece only shows in its setting.

const THORNS = [{ name: 'Ronces mortelles', cells: [[105], [104]] }];

const HAZARDS = {
  2: THORNS, 6: THORNS, 10: THORNS, 13: THORNS, 15: THORNS,
  4: [{ name: 'Lave', cells: [[127], [128]] }],
  7: [{ name: 'Lave', cells: [[190], [191]] }],
  8: [{ name: 'Pieux enflammés', cells: [[223], [224]] }],
  11: [{ name: 'Pics', cells: [[230]] }, { name: 'Pics au plafond', cells: [[229]] }],
  16: [
    { name: 'Pics', cells: [[78]] },
    { name: 'Grands pics', cells: [[79], [78]] },
    { name: 'Pics au plafond', cells: [[83], [84]] },
    { name: 'Bassin mortel', cells: [[85], [86]] },
  ],
};

// Parts (stamps with their own blocks) for the deadly blocks of `theme`.
export function hazardParts(theme) {
  return (HAZARDS[theme] || []).map((hz) => ({ kind: 'stamp', name: hz.name, w: 1, h: hz.cells.length, cells: hz.cells }));
}
