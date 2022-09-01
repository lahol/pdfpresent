# pdfpresent #

pdfpresent is intended as a simple presentation software for slides in PDF format.
The main idea is to provide two separate windows, one for the main presentation and one
for the so called console. Here, the current time and the current prerender status are shown.
There is support for notes (as optionally provided by the LaTeX beamer
class), too, which
should be recognized automatically.

## Installation ##

Just run

    $ make
    # make install

This will install the program into /usr/bin. If you want to install
it in another location just use
    
    # make PREFIX=/your/app/dir install

## Run ##
Simply run

    $ pdfpresent presentation.pdf

## Command line arguments ##
| Argument | Description |
| --- | --- |
| `-c`, `--console=value` | Show console at startup |
| `-n`, `--notes=value` | Assume there are notes or not, guess value |
| `-p`, `--preview=value` | Show preview of next slide in console |
| `-h`, `--height=N` | Use pixmap of this height for prerendering |
| `--no-cache` | Do not cache pages |

## Key-Bindings ##

### Normal mode ###

| Action | Keys |
| --- | --- |
| Previous page | `Left/Up/Prior` |
| Next page | `Right/Down/Next/Space/Return` |
| First page | `Home` |
| Last page | `End` |
| Turn on/off/guess notes | `n` |
| Turn on/off console | `c` |
| Reload document | `r` |
| Turn on/off preview | `p` |
| Go back to last page after jump | `Ctrl+O` |
| Toggle fullscreen | `f` |
| Go to overview mode | `Tab` |
| Quit | `q` |

### Overview mode ###

| Action | Keys |
| --- | --- |
| Move left | `Left` |
| Move right | `Right` |
| Move up | `Up` |
| Move down | `Down` |
| Select page | `Enter` |
| Reload document | `r` |
| Toggle fullscreen | `f` |
| Go to normal mode | `Tab/Escape` |
| Quit | `q` |

## License ##

pdfpresent is released under a MIT license. See LICENSE for details.
