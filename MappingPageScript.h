#ifndef SKYBADGER_MAPPING_PAGE_SCRIPT_H
#define SKYBADGER_MAPPING_PAGE_SCRIPT_H
static const char mappingPageScript[] PROGMEM = R"HTML(<script>
function updateMappedForm(form, changed) {
  const source = form.elements.namedItem('source');
  const mode = form.elements.namedItem('mode');
  if (!source || !mode) return;
  const selected = source.options[source.selectedIndex];
  const caps = Number(selected.dataset.caps || 31);
  const masks = [1, 2, 4, 8, 16];
  for (const option of mode.options) option.disabled = !(caps & masks[Number(option.value)]);
  if (mode.options[mode.selectedIndex].disabled) {
    const preferred = [1, 2, 0, 3, 4].find(value => caps & masks[value]);
    mode.value = String(preferred);
    changed = true;
  }
  const value = Number(mode.value);
  const writable = form.elements.namedItem('writeable');
  writable.disabled = value === 0 || value === 3;
  if (writable.disabled) writable.checked = false;
  else if (changed) writable.checked = true;
  if (changed) {
    const limits = value === 2 ? [0, 1024, 1] : value === 4 ? [0, 4095, 1] :
      value === 3 ? [Number(selected.dataset.min), Number(selected.dataset.max), Number(selected.dataset.step)] : [0, 1, 1];
    ['min', 'max', 'step'].forEach((name, index) => form.elements.namedItem(name).value = limits[index]);
    if (value === 3) { form.elements.namedItem('scale').value = 1; form.elements.namedItem('offset').value = 0; }
  }
}
document.addEventListener('DOMContentLoaded', () => {
  for (const form of document.querySelectorAll('form[data-mapped-source]')) updateMappedForm(form, false);
});
</script>)HTML";
#endif
