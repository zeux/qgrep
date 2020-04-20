" Global options
let s:globalopts = {
    \ 'guicursor': 'a:blinkon0',
    \ 'hlsearch': 0,
    \ 'imdisable': 1,
    \ 'mouse': 'n',
    \ 'mousef': 0,
    \ 'showcmd': 0,
    \ 'timeout': 1,
    \ 'timeoutlen': 0,
    \ 'updatetime': 4000,
    \ }

" Key mappings
let s:keymap = {
    \ 's:onDeleteChar(%s, -1)':         ['<BS>', '<C-]>'],
    \ 's:onDeleteChar(%s, 0)':          ['<Del>'],
    \ 's:onMoveLine(%s, "j")':          ['<C-j>', '<Down>'],
    \ 's:onMoveLine(%s, "k")':          ['<C-k>', '<Up>'],
    \ 's:onMoveLine(%s, "gg")':         ['<Home>', '<kHome>'],
    \ 's:onMoveLine(%s, "G")':          ['<End>', '<kEnd>'],
    \ 's:onMoveLine(%s, "pk")':         ['<PageUp>', '<kPageUp>'],
    \ 's:onMoveLine(%s, "pj")':         ['<PageDown>', '<kPageDown>'],
    \ 's:onMoveCursor(%s, -1)':         ['<C-h>', '<Left>', '<C-^>'],
    \ 's:onMoveCursor(%s, +1)':         ['<C-l>', '<Right>'],
    \ 's:onMoveCursor(%s, -1000)':      ['<C-Home>', '<C-kHome>'],
    \ 's:onMoveCursor(%s, +1000)':      ['<C-End>', '<C-kEnd>'],
    \ 's:onInsertRegister(%s, "*")':    ['<S-Insert>'],
    \ 'qgrep#close()':                  ['<Esc>', '<C-c>'],
    \ }

" Per-mode history (save old state)
let s:history = {}

function! s:state()
    return s:state
endfunction

function! s:modecall(state, name, args)
    return call(printf('qgrep#%s#%s', a:state.mode, a:name), [a:state] + a:args)
endfunction

function! s:echoHighlight(group, text)
    execute 'echohl' a:group
    echon a:text
    echohl None
endfunction

function! s:renderPrompt(state)
    let state = a:state
    let text = state.input
    let cursor = state.cursor

    redraw
    call s:echoHighlight('QgrepPromptPrompt', '>>> ')

    if cursor < 0
        call s:echoHighlight('QgrepPromptTextSelected', text)
    else
        call s:echoHighlight('QgrepPromptText', strpart(text, 0, cursor))
        call s:echoHighlight('QgrepPromptCursor', strpart(text, cursor, 1))
        call s:echoHighlight('QgrepPromptText', strpart(text, cursor + 1))

        if cursor >= len(text)
            call s:echoHighlight('QgrepPromptCursor', '_')
        endif
    endif
endfunction

function! s:renderResults(lines, maxheight)
    let height = min([len(a:lines), a:maxheight])
    setlocal modifiable
    silent! execute '%d _'
    silent! execute 'resize' height
    call setline(1, map(copy(a:lines), '"  ".v:val'))
    setlocal nomodifiable
endfunction

function! s:renderStatus(state, matches, time)
    let res = []

    call add(res, 'qgrep '.a:state.mode)
    let res += s:modecall(a:state, 'getStatus', [])

    if a:matches < a:state.config.limit
        call add(res, printf("%d matches", a:matches))
    else
        call add(res, printf("%d+ matches", a:matches))
    endif

    let groups = ['QgrepStatusOdd', 'QgrepStatusEven']

    let &l:statusline = join(map(copy(res), 'len(v:val) ? ("%#" . groups[v:key % len(groups)] . "# " . v:val . " %*") : " "'), '') . printf('%%=%.f ms', a:time)
endfunction

function! s:diffms(start, end)
    return str2float(reltimestr(reltime(a:start, a:end))) * 1000
endfunction

function! s:redrawResults(state, results)
    let state = a:state
    let lines = s:modecall(state, 'formatResults', [a:results])
    call s:renderResults(lines, state.config.maxheight)
    call cursor(state.line, 1)
    call s:renderStatus(state, len(a:results), 0)
endfunction

function! s:updateResults(state)
    let state = a:state
    let pattern = s:modecall(state, 'parseInput', [state.input])

    if has_key(state, 'lastpattern') && state.lastpattern ==# pattern
        return
    end

    let start = reltime()
    let results = s:modecall(state, 'getResults', [pattern])
    call s:redrawResults(state, results)
    let end = reltime()

    let state.results = results
    let state.lastpattern = pattern

    call s:renderStatus(state, len(results), s:diffms(start, end))
endfunction

function! s:onInputChanged(state)
    if !has('autocmd') || a:state.config.lazyupdate == 0
        call s:updateResults(a:state)
    endif
    call s:renderPrompt(a:state)
endfunction

function! s:onPromptChanged(state)
    call s:renderPrompt(a:state)
endfunction

function! s:tryReplaceInput(state)
    let state = a:state

    if state.cursor < 0
        let state.input = ''
        let state.cursor = 0
        let state.line = 0
    endif
endfunction

function! s:onInsertChar(state, char)
    let state = a:state
    call s:tryReplaceInput(state)
    let state.input = strpart(state.input, 0, state.cursor) . a:char . strpart(state.input, state.cursor)
    let state.cursor += len(a:char)
    call s:onInputChanged(state)
endfunction

function! s:onInsertRegister(state, reg)
    let text = getreg(a:reg)
    let text = substitute(text, "[^ -~]", "", "g")
    call s:onInsertChar(a:state, text)
endfunction

function! s:onDeleteChar(state, offset)
    let state = a:state
    call s:tryReplaceInput(state)
    let state.input = strpart(state.input, 0, state.cursor + a:offset) . strpart(state.input, state.cursor + a:offset + 1)
    if state.cursor > 0 && a:offset < 0
        let state.cursor -= 1
    endif
    call s:onInputChanged(state)
endfunction

function! s:onMoveCursor(state, diff)
    let state = a:state
    if state.cursor < 0
        let state.cursor = len(state.input)
    endif
    let state.cursor += a:diff
    let state.cursor = max([0, min([state.cursor, len(state.input)])])
    call s:onPromptChanged(state)
endfunction

function! s:onMoveLine(state, type)
    let state = a:state
    let motion = (a:type[0] == 'p') ? winheight(0) . a:type[1:] : a:type
    execute 'keepjumps' 'normal!' motion '0'
    let state.line = line('.')
    call s:onPromptChanged(state)
endfunction

function! s:initSyntax()
    if !qgrep#utils#syntax()
        return
    endif

    syntax clear

    highlight default link QgrepPromptPrompt Comment
    highlight default link QgrepPromptText Normal
    highlight default link QgrepPromptTextSelected Visual
    highlight default link QgrepPromptCursor Constant

    highlight default link QgrepStatusOdd LineNr
    highlight default link QgrepStatusEven None

    call qgrep#utils#syntax('Match')
endfunction

function! s:initOptions(state)
    " Global options
    let a:state.globalopts = {}

    for [k, v] in items(s:globalopts)
        if exists('+'.k)
            let a:state.globalopts[k] = eval('&'.k)
            execute 'let &'.k.'='.string(v)
        endif
    endfor

    " Local options
    setlocal bufhidden=unload
    setlocal nobuflisted
    setlocal buftype=nofile
    setlocal colorcolumn=0
    setlocal nocursorcolumn
    setlocal cursorline
    setlocal foldcolumn=0
    setlocal nofoldenable
    setlocal nolist
    setlocal nomodifiable
    setlocal number
    setlocal numberwidth=4
    setlocal norelativenumber
    setlocal noreadonly
    setlocal nospell
    setlocal noswapfile
    setlocal winfixheight
    setlocal nowrap

    if has('conceal')
        setlocal concealcursor=n
        setlocal conceallevel=2
    endif

    " Custom options
    if a:state.config.lazyupdate
        let &updatetime = (a:state.config.lazyupdate > 1) ? a:state.config.lazyupdate : 250
    endif
endfunction

function! s:initKeys(state, stateexpr)
    let charcmd = 'nnoremap <buffer> <silent> %s :call <SID>onInsertChar(%s, "%s")<CR>'

    " fix arrow keys
    if (has('termresponse') && v:termresponse =~ "\<Esc>") || &term =~? '\vxterm|<k?vt|gnome|screen|linux|ansi'
        for mapping in ['\A <Up>', '\B <Down>', '\C <Right>', '\D <Left>']
            execute 'nnoremap <buffer> <silent> <Esc>['.mapping
        endfor
    endif

    " normal keys
    for ch in range(32, 126)
        execute printf(charcmd, printf('<char-%d>', ch), a:stateexpr, escape(nr2char(ch), '"|\'))
    endfor

    " keypad numeric keys
    for ch in range(0, 9)
        execute printf(charcmd, printf('<k%d>', ch), a:stateexpr, ch)
    endfo

    " keypad non-numeric keys
    let kprange = { 'Plus': '+', 'Minus': '-', 'Divide': '/', 'Multiply': '*', 'Point': '.' }

    for [key, ch] in items(kprange)
        execute printf(charcmd, printf('<k%s>', key), a:stateexpr, ch)
    endfor

    " Ctrl-R + register
    " note: we can't use chords and have to resort to getchar() because timeoutlen is 0 (it's 0 to disable user mappings i.e. <Leader>letter)
    execute printf('nnoremap <buffer> <silent> <C-R> :call <SID>onInsertRegister(%s, nr2char(getchar()))<CR>', a:stateexpr)

    " special keys
    for keymap in [s:keymap, a:state.config.keymap]
        for [expr, keys] in items(keymap)
            let expr = stridx(expr, '%s') < 0 ? expr : printf(expr, a:stateexpr)
            let expr = expr[0:1] == 's:' ? '<SID>'.expr[2:] : expr
            for key in keys
                execute 'nnoremap <buffer> <silent>' key ':call' expr '<CR>'
            endfor
        endfor
    endfor
endfunction

function! s:iscmdwin()
    let v:errmsg = ""
    silent! noautocmd wincmd p
    silent! noautocmd wincmd p
    return v:errmsg =~ '^E11:'
endfunction

function! s:mergeConfig(target, source)
    for p in items(a:source)
        if has_key(a:target, p[0]) && type(a:target[p[0]]) == type({})
            call extend(a:target[p[0]], p[1])
        else
            let a:target[p[0]] = copy(p[1])
        endif
    endfor
endfunction

function! s:open(args)
    if exists('s:state') || s:iscmdwin()
        return
    endif

    " get state from history or from sensible defaults
    let mode = empty(a:args) ? g:qgrep.mode : a:args[0]
    let state = (has_key(s:history, mode)) && (len(a:args) <= 1) ? copy(s:history[mode]) : {'cursor': 0, 'input': '', 'line': 0, 'results': [], 'mode': mode, 'config': {}}
    let s:state = state

    if len(a:args) > 1
        let state.input = a:args[1]
    endif

    " make sure that any existing input triggers the 'selected' state
    let state.cursor = len(state.input) ? -1 : 0

    " add all entries from global and mode-specific config to state config
    let oldconfig = state.config
    let state.config = copy(g:qgrep)
    call s:mergeConfig(state.config, has_key(g:qgrep, mode) ? g:qgrep[mode] : {})

    " save commands to current window state
    let state.winrestore = [winrestcmd(), &lines, winnr('$')]

    " create Qgrep window
    silent! keepalt botright 1new Qgrep

    " initialize buffer
    call s:initOptions(state)
    call s:initSyntax()
    call s:initKeys(state, '<SID>state()')

    " custom mode initializer
    call s:modecall(state, 'init', [])

    " if we have some (valid) results from history, display them in new buffer
    if has_key(state, 'results') && oldconfig ==# state.config
        call s:redrawResults(state, state.results)
    elseif has_key(state, 'lastpattern')
        unlet! state.lastpattern
    endif

    " update prompt and results
    call s:update(state)
endfunction

function! s:close()
    if exists('s:state')
        let state = s:state
        let s:history[state.mode] = state

        for [k, v] in items(state.globalopts)
            silent! execute 'let &'.k.'='.string(v)
        endfor

        bdelete!

        if state.winrestore[1] >= &lines && state.winrestore[2] == winnr('$')
            execute state.winrestore[0]
        endif

        echo
        unlet! s:state
    endif
endfunction

function! s:update(state)
    call s:updateResults(a:state)
    call s:renderPrompt(a:state)
endfunction

function! qgrep#open(...)
    noautocmd call s:open(a:000)
endfunction

function! qgrep#close()
    noautocmd call s:close()
    wincmd p
endfunction

function! qgrep#update()
    if exists('s:state')
        unlet! s:state.lastpattern
        call s:update(s:state)
    endif
endfunction

function! qgrep#acceptSelection(...)
    let state = s:state
    let line = line('.') - 1

    if line >= 0 && line < len(state.results)
        call qgrep#close()

        let result = substitute(state.results[line], '\%o33\[.\{-}m', '', 'g')
        call s:modecall(state, 'acceptResult', [state.input, result] + a:000)
    endif
endfunction

function! qgrep#execute(args, ...)
    let path = g:qgrep.qgrep
    let input = a:0 ? (type(a:1) == type('') ? a:1 : join(a:1, "\n")) : ''

    try
        if path[0:7] == 'libcall:'
            let args = join(a:args, "\n")
            let args = len(input) ? args . "\2" . input : args
            let results = libcall(path[8:], 'qgrepVim', args)
        else
            let args = map(copy(a:args), 'shellescape(v:val)')
            let cmd = path . ' ' . join(args, ' ')
            let results = len(input) ? system(cmd, input) : system(cmd)
        endif

        return split(results, "\n")
    catch
        return ['Error: ' . v:exception]
    endtry
endfunction

function! qgrep#filter(state, pattern, input)
    return qgrep#execute(['filter', a:state.config.searchtype, qgrep#utils#syntax() && has('conceal') ? 'H' : '', 'L'.a:state.config.limit, a:pattern], a:input)
endfunction

function! qgrep#selectProject(...)
    if a:0
        let g:qgrep.project = a:1
        if exists('s:state')
            let s:state.config.project = a:1
            call qgrep#update()
        endif
        if exists('s:grepprg')
            call call('qgrep#replaceGrep', s:grepprg)
        endif
    else
        call qgrep#selectProjectGUI()
    endif
endfunction

function! qgrep#selectProjectGUI()
    let projects = qgrep#execute(['projects'])

    if has('menu') && has('gui_running')
        try
            nunmenu ]Qgrep
        catch
        endtry

        execute 'nnoremenu' ']Qgrep.Select\ project:' '<Nop>'

        for i in range(0, len(projects))
            let proj = i == 0 ? '*' : projects[i - 1]
            let pref = i < 10 ? '&'.i : '\ '
            execute 'nnoremenu' '<silent>' ']Qgrep.'.pref.'\ '.proj ':call qgrep#selectProject("'.proj.'")<CR>'
        endfor
        popup ]Qgrep
    else
        let lines = copy(projects)
        call map(lines, 'printf("%d. %s", v:key + 1, v:val)')
        call insert(lines, 'Select project (*):')

        let choice = inputlist(lines)
        if choice >= 0 && choice <= len(projects)
            call qgrep#selectProject(choice == 0 ? '*' : projects[choice - 1])
        endif
    endif
endfunction

function! qgrep#replaceGrep(...)
    let s:grepprg = a:000
    let opts = empty(s:grepprg) ? ['<project>'] : s:grepprg
    let args = map(copy(opts), 'shellescape(v:val == "<project>" ? g:qgrep.project : v:val)')
    let path = g:qgrep.qgrep
    let path = path[0:7] == 'libcall:' ? path[8:] : path
    let &grepprg = path . ' search ' . join(args, ' ')
endfunction

function! qgrep#change(file)
    call qgrep#execute(['change', '*', a:file])
endfunction

if has('autocmd')
    augroup QgrepAug
        autocmd!
        autocmd BufLeave Qgrep call s:close()
        autocmd CursorHold Qgrep if s:state.config.lazyupdate | call s:update(s:state) | endif
    augroup END
endif
