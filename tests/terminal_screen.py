"""Minimal VT screen model for termbox's absolute-positioned cell updates."""
import codecs
import unicodedata


class TerminalScreen:
    def __init__(self, columns, rows):
        self.columns = columns
        self.rows = rows
        self.row = 0
        self.column = 0
        self.foreground = None
        self.background = None
        self.attributes = set()
        self.cells = []
        self.pending = ""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self._clear()

    def _style(self):
        return (self.foreground, self.background,
                tuple(sorted(self.attributes)))

    def _clear(self):
        cell = (" ", self._style())
        self.cells = [[cell for _ in range(self.columns)]
                      for _ in range(self.rows)]

    def _sgr(self, parameters):
        values = [int(value) if value else 0
                  for value in parameters.split(";")] if parameters else [0]
        index = 0
        while index < len(values):
            value = values[index]
            if value == 0:
                self.foreground = None
                self.background = None
                self.attributes.clear()
            elif value in (1, 2, 3, 4, 5, 7, 8):
                self.attributes.add(value)
            elif value == 22:
                self.attributes.discard(1)
                self.attributes.discard(2)
            elif value in (23, 24, 25, 27, 28):
                self.attributes.discard({23: 3, 24: 4, 25: 5,
                                         27: 7, 28: 8}[value])
            elif 30 <= value <= 37 or 90 <= value <= 97:
                self.foreground = value
            elif value == 39:
                self.foreground = None
            elif 40 <= value <= 47 or 100 <= value <= 107:
                self.background = value
            elif value == 49:
                self.background = None
            elif value in (38, 48) and index + 2 < len(values) and \
                    values[index + 1] == 5:
                colour = (5, values[index + 2])
                if value == 38:
                    self.foreground = colour
                else:
                    self.background = colour
                index += 2
            index += 1

    def _csi(self, parameters, command):
        public = parameters.lstrip("?<>")
        values = [int(value) if value else 0
                  for value in public.split(";")] if public else []
        if command in ("H", "f"):
            self.row = max(0, (values[0] if values else 1) - 1)
            self.column = max(0, (values[1] if len(values) > 1 else 1) - 1)
        elif command == "G":
            self.column = max(0, (values[0] if values else 1) - 1)
        elif command == "d":
            self.row = max(0, (values[0] if values else 1) - 1)
        elif command == "J" and (values[0] if values else 0) == 2:
            self._clear()
        elif command == "K":
            mode = values[0] if values else 0
            start = 0 if mode in (1, 2) else self.column
            end = self.columns if mode in (0, 2) else self.column + 1
            if 0 <= self.row < self.rows:
                for column in range(max(0, start), min(self.columns, end)):
                    self.cells[self.row][column] = (" ", self._style())
        elif command == "m":
            self._sgr(public)

    def feed(self, data):
        text = self.pending + self.decoder.decode(data)
        index = 0
        while index < len(text):
            character = text[index]
            if character == "\x1b":
                if index + 1 >= len(text):
                    break
                if text[index + 1] == "[":
                    end = index + 2
                    while end < len(text) and not "@" <= text[end] <= "~":
                        end += 1
                    if end >= len(text):
                        break
                    self._csi(text[index + 2:end], text[end])
                    index = end + 1
                    continue
                if text[index + 1] == "(" and index + 2 >= len(text):
                    break
                index += 3 if text[index + 1] == "(" else 2
                continue
            if character == "\r":
                self.column = 0
            elif character == "\n":
                self.row += 1
            elif character == "\b":
                self.column = max(0, self.column - 1)
            elif character >= " ":
                width = 0 if unicodedata.combining(character) else (2 if unicodedata.east_asian_width(character) in ("W", "F") else 1)
                if 0 <= self.row < self.rows and 0 <= self.column < self.columns:
                    if width:
                        self.cells[self.row][self.column] = (character, self._style())
                        if width == 2 and self.column + 1 < self.columns:
                            self.cells[self.row][self.column + 1] = ("", self._style())
                    elif self.column:
                        old, style = self.cells[self.row][self.column - 1]
                        self.cells[self.row][self.column - 1] = (old + character, style)
                self.column += width
            index += 1
        self.pending = text[index:]

    def snapshot(self):
        return tuple(tuple(row) for row in self.cells)

    def text(self):
        return "\n".join("".join(cell[0] for cell in row) for row in self.cells)
