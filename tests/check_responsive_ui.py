"""Render representative setup controls with the firmware's actual CSS in Chrome."""
import html
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ResponsiveUi(unittest.TestCase):
    def test_setup_builders_share_responsive_styles(self):
        source = (ROOT / 'ESP8266_relayhandler.h').read_text()
        self.assertIn('htmlForm += FPSTR(setupPageStyle);', source)
        self.assertIn('width=device-width, initial-scale=1', source)
        for function in ['sendDeviceSetup', 'sendDriver0Setup']:
            start = re.search(r'void ' + function + r'\([^;{}]*\)\s*\{', source).start()
            self.assertIn('setupFormBuilderHeader(', source[start:source.index('\n}', start)])

    def test_browser_layout_at_phone_tablet_desktop_widths(self):
        style = (ROOT / 'SetupPageStyle.h').read_text().split('R"HTML(', 1)[1].split(')HTML"', 1)[0]
        controls = ''.join('<label><span>' + name + '</span></label>' + control + '<br>'
                           for name, control in [('Switch name', '<input value="A long switch name">'),
                            ('Description', '<input value="Long description">'),
                            ('Switch type', '<select><option>Digital PWM output</option></select>'),
                            ('Minimum value', '<input type="number" value="0">')])
        markup = style + '<div class="container"><div class="row float-left"><div class="col-sm-6">' \
                 '<p>' + ('LongHostname' * 20) + '</p><form><label>Client ID <input type="number" value="123"></label>' \
                 '<fieldset><legend>Switch settings</legend>' + controls + \
                 '<label><input type="radio" name="writeable">Writable</label>' \
                 '<input type="submit" value="Update"></fieldset></form></div></div>' \
                 '<form class="form-inline"><input type="submit" value="Restart device"></form></div>'
        frames = ''.join('<iframe style="display:block;border:0;width:%dpx;height:1000px" srcdoc="%s"></iframe>'
                         % (width, html.escape(markup, quote=True)) for width in [320, 390, 768, 1440])
        script = '''<script>window.addEventListener('load',()=>{
          let failures=[];
          for(const frame of document.querySelectorAll('iframe')){
            const d=frame.contentDocument,w=frame.contentWindow.innerWidth;
            if(d.documentElement.scrollWidth>w) failures.push(w+':page overflow');
            for(const e of d.querySelectorAll('form,input,select,fieldset')){
              const r=e.getBoundingClientRect();
              if(r.left<0 || r.right>w+.5) failures.push(w+':'+e.tagName+' overflow');
            }
          }
          document.getElementById('result').textContent=failures.length?failures.join(';'):'PASS:320,390,768,1440';
        });</script>'''
        target = ROOT / '.pio/responsive-ui-check.html'
        target.write_text('<!doctype html><meta charset="utf-8">' + frames + '<pre id="result">PENDING</pre>' + script)
        browser = pathlib.Path('C:/Program Files/Google/Chrome/Application/chrome.exe')
        result = subprocess.run([str(browser), '--headless', '--disable-gpu', '--no-first-run',
                                 '--user-data-dir=' + str(ROOT / '.pio/ui-browser-profile'),
                                 '--virtual-time-budget=5000', '--dump-dom', target.as_uri()],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr[-500:])
        self.assertIn('<pre id="result">PASS:320,390,768,1440</pre>', result.stdout,
                      re.search(r'<pre id="result">.*?</pre>', result.stdout).group() if '<pre id="result">' in result.stdout else result.stderr[-500:])


if __name__ == '__main__':
    unittest.main()
