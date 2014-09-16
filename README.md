## What's this?

``mrubyd`` reduces memory usage of mruby scripts run on operating systems
implement copy-on-write virutual memory system.
Though it is a proof-of-concept impementaion, our experiments show
10 `mruby` processes consume 15M bytes (2+1.3\*N) without `mrubyd`,
and 1 `mrubyd` + 10 `mrubyc` processes consume only 7M bytes (3+0.4\*N).


## How to use

 1. Start `mrubyd`:

    ```
    % mrubyd
    %
    ```

 2. Invoke `mrubyc` to run a ruby script:

    ```
    % mrubyc -e 'p "Hello"'
    "Hello"

    % mrubyc a.rb
    "AAhhhhhhhhhhhhhhhhh"
    ```

## TODO
- kill worker process when mrubyc received some signals (e.g. SIGINT),
  or relay signals.


## License

Copyright (c) 2014 Internet Initiative Japan Inc.

Permission is hereby granted, free of charge, to any person obtaining a 
copy of this software and associated documentation files (the "Software"), 
to deal in the Software without restriction, including without limitation 
the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the 
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in 
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
DEALINGS IN THE SOFTWARE.
