local M = {}

local function plugin_root()
  local source = debug.getinfo(1, "S").source:sub(2)
  return vim.fn.fnamemodify(source, ":p:h:h:h")
end

local function root_dir(bufnr)
  local name = vim.api.nvim_buf_get_name(bufnr)
  if vim.fs and vim.fs.root then
    return vim.fs.root(name, { ".git", ".jj", "assembler", "disassembler" }) or vim.fn.getcwd()
  end
  local dir = vim.fn.fnamemodify(name, ":p:h")
  while dir and dir ~= "/" do
    if vim.fn.isdirectory(dir .. "/.git") == 1 or vim.fn.isdirectory(dir .. "/.jj") == 1 then
      return dir
    end
    dir = vim.fn.fnamemodify(dir, ":h")
  end
  return vim.fn.getcwd()
end

function M.setup(opts)
  opts = opts or {}
  local python = opts.python or vim.fn.exepath("python3")
  if python == "" then
    python = "python3"
  end
  local server = opts.server or (plugin_root() .. "/server/e16_lsp.py")
  local highlights = {
    e16DataMovement = "#22D3EE",
    e16Memory = "#60A5FA",
    e16Arithmetic = "#FACC15",
    e16Bitwise = "#E879F9",
    e16Shift = "#4ADE80",
    e16Compare = "#E5E7EB",
    e16Branch = "#F87171",
    e16CallStack = "#06B6D4",
    e16InterruptSystem = "#EF4444",
    e16Special = "#22C55E",
    e16Helper = "#C084FC",
  }

  for name, color in pairs(highlights) do
    vim.api.nvim_set_hl(0, "@lsp.type." .. name .. ".e16", { fg = color, bold = true })
  end

  vim.api.nvim_create_autocmd("FileType", {
    pattern = "e16",
    group = vim.api.nvim_create_augroup("e16_lsp", { clear = true }),
    callback = function(args)
      vim.lsp.start({
        name = "e16-lsp",
        cmd = { python, server },
        root_dir = root_dir(args.buf),
        capabilities = vim.lsp.protocol.make_client_capabilities(),
      }, {
        bufnr = args.buf,
      })
    end,
  })
end

return M
