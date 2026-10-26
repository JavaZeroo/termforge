use std::env;
use std::fs;
use std::sync::{Arc, Mutex};

use alacritty_terminal::event::{Event, EventListener};
use alacritty_terminal::grid::{Dimensions, GridCell};
use alacritty_terminal::index::{Column, Line};
use alacritty_terminal::term::{
    Config, Term,
    cell::{Cell, Flags},
};
use alacritty_terminal::vte::ansi::{Color, NamedColor, Processor};
use serde_json::{Value, json};

#[derive(Clone, Default)]
pub(crate) struct Listener(pub(crate) Arc<Mutex<Vec<Event>>>);

impl EventListener for Listener {
    fn send_event(&self, event: Event) {
        self.0.lock().unwrap().push(event);
    }
}

pub(crate) struct Size {
    pub(crate) columns: usize,
    pub(crate) rows: usize,
}

impl Dimensions for Size {
    fn total_lines(&self) -> usize {
        self.rows
    }
    fn screen_lines(&self) -> usize {
        self.rows
    }
    fn columns(&self) -> usize {
        self.columns
    }
}

fn color(value: Color) -> i32 {
    match value {
        Color::Named(NamedColor::Foreground | NamedColor::Background) => -1,
        Color::Named(named) if (named as usize) < 16 => named as i32,
        Color::Indexed(index) if index < 16 => index as i32,
        _ => -2,
    }
}

fn cell_value(cell: &Cell) -> Value {
    let mut text = if cell.is_empty() {
        String::new()
    } else {
        cell.c.to_string()
    };
    if let Some(zero_width) = cell.zerowidth() {
        text.extend(zero_width);
    }
    let width = if cell.flags().contains(Flags::WIDE_CHAR) {
        2
    } else if text.is_empty() || cell.flags().contains(Flags::WIDE_CHAR_SPACER) {
        0
    } else {
        1
    };
    json!({
        "text": text,
        "width": width,
        "style": {
            "fg": color(cell.fg),
            "bg": color(cell.bg),
            "bold": cell.flags().contains(Flags::BOLD),
        },
    })
}

fn rows(term: &Term<Listener>, first: i32, count: usize) -> Vec<Value> {
    let grid = term.grid();
    (0..count)
        .map(|row| {
            let line = Line(first + row as i32);
            (0..grid.columns())
                .map(|column| cell_value(&grid[line][Column(column)]))
                .collect()
        })
        .collect()
}

pub(crate) fn snapshot(term: &mut Term<Listener>, listener: &Listener, offset: usize) -> Value {
    let history = term.grid().history_size();
    let grid = term.grid();
    let cursor = &grid.cursor;
    let row = cursor.point.line.0;
    let column = cursor.point.column.0;
    let wrap_pending = cursor.input_needs_wrap;
    let current_style = json!({
        "fg": color(cursor.template.fg),
        "bg": color(cursor.template.bg),
        "bold": cursor.template.flags.contains(Flags::BOLD),
    });
    let cells = rows(term, 0, grid.screen_lines());
    let history_cells = rows(term, -(history as i32), history);
    let events = {
        let mut pending = listener.0.lock().unwrap();
        std::mem::take(&mut *pending)
    };
    let replies: Vec<String> = events
        .into_iter()
        .filter_map(|event| match event {
            Event::PtyWrite(text) => Some(text),
            _ => None,
        })
        .collect();
    json!({
        "offset": offset,
        "screen": {
            "row": row,
            "column": column,
            "wrap_pending": wrap_pending,
            "cells": cells,
            "history": history_cells,
            "current_style": current_style,
        },
        "mode_bits": term.mode().bits(),
        "grapheme_overflows": term.grapheme_overflows(),
        "replies": replies,
    })
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    if !(6..=7).contains(&args.len()) {
        return Err("usage: alacritty_trace input rows columns history chunks.txt [--resize=RxC]".into());
    }
    let resize = if args.len() == 7 {
        let (rows, columns) = args[6]
            .strip_prefix("--resize=")
            .and_then(|value| value.split_once('x'))
            .ok_or("invalid resize option")?;
        let rows = rows.parse::<usize>().map_err(|_| "invalid resize rows")?;
        let columns = columns.parse::<usize>().map_err(|_| "invalid resize columns")?;
        if rows == 0 || columns == 0 {
            return Err("resize dimensions exceed the trace budget".into());
        }
        Some((rows, columns))
    } else {
        None
    };
    let rows = args[2].parse::<usize>().map_err(|_| "invalid rows")?;
    let columns = args[3].parse::<usize>().map_err(|_| "invalid columns")?;
    let history = args[4].parse::<usize>().map_err(|_| "invalid history")?;
    if rows == 0 || rows > 1000 || !(1..=1000).contains(&columns) || history > 100000 {
        return Err("candidate dimensions or history exceed the trace budget".into());
    }
    let data = fs::read(&args[1]).map_err(|error| error.to_string())?;
    if data.len() > 1024 * 1024 {
        return Err("trace input exceeds 1 MiB".into());
    }
    let plan = fs::read_to_string(&args[5]).map_err(|error| error.to_string())?;
    let listener = Listener::default();
    let config = Config {
        scrolling_history: history,
        ..Config::default()
    };
    let mut term = Term::new(config, &Size { columns, rows }, listener.clone());
    let mut parser: Processor = Processor::new();
    let mut offset = 0;
    let mut traced_cells = 0usize;
    let mut checkpoint = |term: &mut Term<Listener>, offset: usize| {
        let count =
            (term.grid().screen_lines() + term.grid().history_size()) * term.grid().columns();
        traced_cells = traced_cells
            .checked_add(count)
            .ok_or("trace cell budget overflow")?;
        if traced_cells > 1_000_000 {
            return Err("trace cell budget exceeded".to_string());
        }
        Ok(snapshot(term, &listener, offset))
    };
    println!("{}", checkpoint(&mut term, offset)?);
    for line in plan.lines() {
        let size = line.parse::<usize>().map_err(|_| "invalid chunk size")?;
        if size == 0 || size > data.len() - offset {
            return Err("chunk plan exceeds input or contains zero".into());
        }
        parser.advance(&mut term, &data[offset..offset + size]);
        offset += size;
        println!("{}", checkpoint(&mut term, offset)?);
    }
    if offset != data.len() {
        return Err("chunk plan does not cover input".into());
    }
    if let Some((rows, columns)) = resize {
        term.resize(Size { columns, rows });
        println!("{}", checkpoint(&mut term, offset)?);
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("alacritty trace failed: {error}");
        std::process::exit(2);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_cell_and_ansi_style_project_explicitly() {
        let empty = cell_value(&Cell::default());
        assert_eq!(empty["text"], "");
        assert_eq!(empty["width"], 0);
        assert_eq!(empty["style"]["fg"], -1);
        let mut wide = Cell::default();
        wide.c = '中';
        wide.flags.insert(Flags::WIDE_CHAR | Flags::BOLD);
        wide.fg = Color::Named(NamedColor::Red);
        let projected = cell_value(&wide);
        assert_eq!(projected["text"], "中");
        assert_eq!(projected["width"], 2);
        assert_eq!(projected["style"]["fg"], 1);
        assert_eq!(projected["style"]["bold"], true);
    }

    #[test]
    fn unsupported_colors_stay_distinct_from_defaults() {
        assert_eq!(color(Color::Named(NamedColor::Foreground)), -1);
        assert_eq!(color(Color::Indexed(42)), -2);
    }
}
