#ifndef SETUP_PAGE_STYLE_H
#define SETUP_PAGE_STYLE_H

static const char setupPageStyle[] PROGMEM = R"HTML(<style>
* { box-sizing: border-box; }
body { margin: 0; color: #222; background: #fff; font: 1rem/1.5 sans-serif; }
.container { width: 100%; max-width: 70rem; margin: auto; padding: 1rem; }
.row { width: 100%; margin: 0 0 1rem; }
.float-left { float: none; }
.col-sm-6, .col-sm-12 { width: 100%; min-width: 0; }
h1, h2, p, li, legend { overflow-wrap: anywhere; }
h1 { margin-top: 0; }
form { width: 100%; max-width: 44rem; margin: 0 auto 1rem; padding: 1rem;
       border: 1px solid #ccc; border-radius: .75rem; }
fieldset { min-width: 0; margin: 0; padding: .75rem; border: 1px solid #ccc; }
legend { max-width: 100%; padding: 0 .25rem; font-weight: bold; }
label { display: inline-block; max-width: 100%; }
label span { display: inline-block; width: 10rem; padding-right: .5rem; }
input, select, textarea, button { max-width: 100%; min-height: 2.75rem; font: inherit; }
input:not([type=radio]):not([type=checkbox]):not([type=submit]), select, textarea {
    width: min(20rem, 100%); padding: .5rem; border: 1px solid #999; }
input[type=radio], input[type=checkbox] { width: auto; min-height: auto; }
input[type=submit], button { width: auto; padding: .5rem 1rem; margin: .5rem 0; cursor: pointer; }
input:focus, select:focus, textarea:focus { outline: 2px solid #175b9d; outline-offset: 2px; }
textarea { vertical-align: top; height: 5rem; resize: vertical; }
.form-inline { display: flex; flex-wrap: wrap; gap: .5rem; align-items: center; }
@media (max-width: 40rem) {
    .container { padding: .75rem; }
    form { padding: .75rem; }
    fieldset { padding: .5rem; }
    label, label span { width: 100%; text-align: left; }
    input:not([type=radio]):not([type=checkbox]):not([type=submit]), select, textarea { width: 100%; }
}
</style>)HTML";
#endif
