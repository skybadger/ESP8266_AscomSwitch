"""Browser-check the HTML emitted by mapping_runtime_tests.exe."""
import html
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MappingUiBrowser(unittest.TestCase):
    def test_generated_mapping_forms_at_four_widths(self):
        fragment = (ROOT / '.pio/mapped-ui-fragment.html').read_text(encoding='utf-8')
        self.assertIn('name="source"', fragment)
        self.assertIn('value="2:32:5" selected', fragment)
        self.assertIn('value="2:32:5" disabled', fragment)
        self.assertIn('MCP4725', fragment)
        self.assertIn('&lt;switch&gt;', fragment)
        self.assertLess(fragment.index('value="n:3"'), fragment.index('value="2:32:5" selected'))
        style = (ROOT / 'SetupPageStyle.h').read_text().split('R"HTML(', 1)[1].split(')HTML"', 1)[0]
        ui_script = (ROOT / 'MappingPageScript.h').read_text().split('R"HTML(', 1)[1].split(')HTML"', 1)[0]
        page = style + ui_script + '<div class="container">' + fragment + '</div>'
        frames = ''.join('<iframe style="display:block;border:0;width:%dpx;height:1600px" srcdoc="%s"></iframe>'
                         % (width, html.escape(page, quote=True)) for width in [320, 390, 768, 1440])
        script = '''<script>window.addEventListener('load',()=>{
          let errors=[];
          for(const frame of document.querySelectorAll('iframe')){
            const d=frame.contentDocument,w=frame.contentWindow.innerWidth;
            const native=d.querySelector('option[value="n:3"]').closest('form');
            const nativeMode=native.elements.namedItem('mode');
            if(!nativeMode.options[3].disabled || !nativeMode.options[4].disabled) errors.push(w+':native capabilities');
            nativeMode.value='2'; nativeMode.dispatchEvent(new frame.contentWindow.Event('change'));
            if(native.elements.namedItem('max').value!=='1024') errors.push(w+':PWM range');
            const external=d.querySelector('option[value="2:32:5"][selected]').closest('form');
            external.elements.namedItem('source').value='4:96:0';
            external.elements.namedItem('source').dispatchEvent(new frame.contentWindow.Event('change'));
            if(external.elements.namedItem('mode').value!=='4' || external.elements.namedItem('max').value!=='4095') errors.push(w+':DAC capabilities');
            if(d.documentElement.scrollWidth>w) errors.push(w+':document');
            for(const e of d.querySelectorAll('form,input,select,fieldset')){
              const r=e.getBoundingClientRect();
              if(r.left<0 || r.right>w+.5) errors.push(w+':'+e.tagName);
            }
          }
          document.getElementById('result').textContent=errors.length?errors.join(';'):'PASS';
        });</script>'''
        target = ROOT / '.pio/mapping-browser-check.html'
        target.write_text('<!doctype html><meta charset="utf-8">' + frames + '<pre id="result">PENDING</pre>' + script, encoding='utf-8')
        result = subprocess.run(['C:/Program Files/Google/Chrome/Application/chrome.exe', '--headless', '--disable-gpu',
                                 '--no-first-run', '--user-data-dir=' + str(ROOT / '.pio/ui-browser-profile'),
                                 '--virtual-time-budget=5000', '--dump-dom', target.as_uri()],
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr[-500:])
        self.assertIn('<pre id="result">PASS</pre>', result.stdout)


if __name__ == '__main__':
    unittest.main()
