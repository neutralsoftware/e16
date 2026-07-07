# e16.nvim

Neovim support for Ember-16 assembly.

It provides:

- `*.e16` filetype detection
- syntax highlighting for instructions, registers, labels, constants, directives, strings, numbers, comments, and addressing punctuation
- grouped instruction colors matching the disassembler palette: memory blue, branches red, arithmetic yellow, bitwise magenta, shifts green, data movement cyan, stack cyan, system red, special registers green
- an LSP server for completions, hover, go-to-definition, document symbols, semantic tokens, and diagnostics

## Install with lazy.nvim

```lua
{
  dir = "/Users/maxvdec/Coding/Projects/E16/nvim/e16.nvim",
  ft = "e16",
  config = function()
    require("e16").setup()
  end,
}
```

## Install with native packages

```sh
mkdir -p ~/.local/share/nvim/site/pack/e16/start
ln -s /Users/maxvdec/Coding/Projects/E16/nvim/e16.nvim ~/.local/share/nvim/site/pack/e16/start/e16.nvim
```

Then add this to `init.lua`:

```lua
require("e16").setup()
```

## Configure manually

```lua
require("e16").setup({
  python = "python3",
  server = "/Users/maxvdec/Coding/Projects/E16/nvim/e16.nvim/server/e16_lsp.py",
})
```

## Check that it is working

Open an `.e16` file:

```sh
nvim /Users/maxvdec/Coding/Projects/E16/assembler/tests/main.e16
```

Inside Neovim run:

```vim
:set filetype?
:LspInfo
```

You should see `filetype=e16` and an attached client named `e16-lsp`.

Useful built-in mappings:

```lua
vim.keymap.set("n", "gd", vim.lsp.buf.definition)
vim.keymap.set("n", "K", vim.lsp.buf.hover)
vim.keymap.set("n", "<leader>ds", vim.lsp.buf.document_symbol)
```

For completion, use Neovim's built-in omnifunc with `<C-x><C-o>`, or connect it to your completion plugin. With `nvim-cmp`, make sure `cmp-nvim-lsp` is installed and your normal LSP source is enabled.
