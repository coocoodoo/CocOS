"""
PNG to C++ Array Converter - GUI Version
Converts a PNG image to a C++ byte array for use in embedded systems.
Optimized for M5Stack Cardputer (ESP32-S3, RGB565 display)
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from pathlib import Path

try:
    from PIL import Image, ImageTk
except ImportError:
    import sys
    print("Error: Pillow library is required. Install it with: pip install Pillow")
    sys.exit(1)


def rgb_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565 format."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb565_swap_bytes(color):
    """Swap bytes of a 16-bit RGB565 value (for displays needing big-endian)."""
    return ((color & 0xFF) << 8) | ((color >> 8) & 0xFF)


def rgb565_to_rgb(color565):
    """Convert RGB565 back to RGB888 for preview."""
    r = ((color565 >> 11) & 0x1F) << 3
    g = ((color565 >> 5) & 0x3F) << 2
    b = (color565 & 0x1F) << 3
    # Fill in the lower bits for better color accuracy
    r |= r >> 5
    g |= g >> 6
    b |= b >> 5
    return (r, g, b)


def convert_png_to_cpp(image_path, format_type="cardputer", var_name=None, transparent_color=None):
    """
    Convert a PNG image to a C++ array string.
    
    Args:
        image_path: Path to the image file
        format_type: Output format (cardputer, rgb565, rgb565_swapped, rgb888, grayscale)
        var_name: Variable name for the array (defaults to filename)
        transparent_color: RGB tuple for transparent pixels (e.g., (0, 0, 0) for black)
    
    Returns:
        tuple: (header_content, width, height, array_length)
    """
    # Load the image
    img = Image.open(image_path)
    width, height = img.size
    
    # Handle transparency - always convert to RGBA first to preserve any alpha
    # This handles palette mode ('P') with transparency, etc.
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    has_alpha = True  # Now we always have alpha channel
    
    # Determine variable name from filename if not provided
    if var_name is None or var_name.strip() == "":
        var_name = Path(image_path).stem
        var_name = ''.join(c if c.isalnum() or c == '_' else '_' for c in var_name)
        if var_name[0].isdigit():
            var_name = '_' + var_name
    
    # Get pixel data
    pixels = list(img.getdata())
    
    # Build the array data based on format
    data = []
    
    # Cardputer uses RGB565 with swapped bytes
    if format_type == "cardputer":
        format_type = "rgb565_swapped"
    
    if format_type == "rgb565":
        for pixel in pixels:
            if has_alpha and len(pixel) == 4:
                r, g, b, a = pixel
                if a < 128 and transparent_color:  # Transparent pixel
                    r, g, b = transparent_color
            else:
                r, g, b = pixel[:3]
            color = rgb_to_rgb565(r, g, b)
            data.append(f"0x{color:04X}")
        data_type = "uint16_t"
        format_name = "RGB565"
    
    elif format_type == "rgb565_swapped":
        for pixel in pixels:
            if has_alpha and len(pixel) == 4:
                r, g, b, a = pixel
                if a < 128 and transparent_color:  # Transparent pixel
                    r, g, b = transparent_color
            else:
                r, g, b = pixel[:3]
            color = rgb_to_rgb565(r, g, b)
            swapped = rgb565_swap_bytes(color)
            data.append(f"0x{swapped:04X}")
        data_type = "uint16_t"
        format_name = "RGB565_SWAPPED"
        
    elif format_type == "rgb888":
        for pixel in pixels:
            if has_alpha and len(pixel) == 4:
                r, g, b, a = pixel
                if a < 128 and transparent_color:
                    r, g, b = transparent_color
            else:
                r, g, b = pixel[:3]
            data.append(f"0x{r:02X}")
            data.append(f"0x{g:02X}")
            data.append(f"0x{b:02X}")
        data_type = "uint8_t"
        format_name = "RGB888"
        
    elif format_type == "grayscale":
        gray_img = img.convert('L')
        gray_pixels = list(gray_img.getdata())
        for g in gray_pixels:
            data.append(f"0x{g:02X}")
        data_type = "uint8_t"
        format_name = "GRAYSCALE"
    
    # Generate the C++ header file (optimized for Cardputer/ESP32)
    header_guard = var_name.upper() + "_H"
    
    lines = [
        f"#ifndef {header_guard}",
        f"#define {header_guard}",
        "",
        "#include <stdint.h>",
        "",
        f"// Image: {Path(image_path).name}",
        f"// Size: {width}x{height} pixels",
        f"// Format: {format_name}",
        f"// Total bytes: {len(data) * (2 if format_type in ('rgb565', 'rgb565_swapped') else 1)}",
        "",
        f"#define {var_name}_width {width}",
        f"#define {var_name}_height {height}",
        "",
        f"static const {data_type} {var_name}[] PROGMEM = {{",
    ]
    
    # Format the data array with 12 values per line (15 for RGB888 bytes)
    values_per_line = 15 if format_type == "rgb888" else 12
    for i in range(0, len(data), values_per_line):
        chunk = data[i:i + values_per_line]
        line = "    " + ", ".join(chunk)
        if i + values_per_line < len(data):
            line += ","
        lines.append(line)
    
    lines.extend([
        "};",
        "",
        f"#endif // {header_guard}",
        ""
    ])
    
    return '\n'.join(lines), width, height, len(data)


class PngToCppConverter:
    def __init__(self, root):
        self.root = root
        self.root.title("PNG to C++ Array Converter (Cardputer)")
        self.root.geometry("800x650")
        self.root.minsize(600, 550)
        
        self.image_path = None
        self.preview_image = None
        self.render_preview_image = None
        
        self.setup_ui()
    
    def setup_ui(self):
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky="nsew")
        
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(5, weight=1)
        
        # --- File Selection ---
        ttk.Label(main_frame, text="Image File:", font=('Segoe UI', 10)).grid(
            row=0, column=0, sticky="w", pady=(0, 5))
        
        file_frame = ttk.Frame(main_frame)
        file_frame.grid(row=0, column=1, sticky="ew", pady=(0, 5))
        file_frame.columnconfigure(0, weight=1)
        
        self.file_entry = ttk.Entry(file_frame)
        self.file_entry.grid(row=0, column=0, sticky="ew", padx=(0, 5))
        
        ttk.Button(file_frame, text="Browse...", command=self.browse_file).grid(
            row=0, column=1)
        
        # --- Options Frame ---
        options_frame = ttk.LabelFrame(main_frame, text="Options", padding="10")
        options_frame.grid(row=1, column=0, columnspan=2, sticky="ew", pady=10)
        options_frame.columnconfigure(1, weight=1)
        options_frame.columnconfigure(3, weight=1)
        
        # Variable name
        ttk.Label(options_frame, text="Variable Name:").grid(
            row=0, column=0, sticky="w", padx=(0, 5))
        self.var_name_entry = ttk.Entry(options_frame, width=25)
        self.var_name_entry.grid(row=0, column=1, sticky="w", padx=(0, 20))
        self.var_name_entry.insert(0, "image")
        
        # Format selection
        ttk.Label(options_frame, text="Format:").grid(
            row=0, column=2, sticky="w", padx=(0, 5))
        self.format_var = tk.StringVar(value="cardputer")
        format_combo = ttk.Combobox(options_frame, textvariable=self.format_var,
                                     values=["cardputer", "rgb565", "rgb565_swapped", "rgb888", "grayscale"],
                                     state="readonly", width=15)
        format_combo.grid(row=0, column=3, sticky="w")
        
        # Format descriptions
        format_desc = ttk.Label(options_frame, 
            text="Cardputer: RGB565 byte-swapped (recommended) | RGB565: 16-bit | RGB888: 24-bit | Grayscale: 8-bit",
            font=('Segoe UI', 8), foreground='gray')
        format_desc.grid(row=1, column=0, columnspan=4, sticky="w", pady=(5, 0))
        
        # Transparent color option
        trans_frame = ttk.Frame(options_frame)
        trans_frame.grid(row=2, column=0, columnspan=4, sticky="w", pady=(10, 0))
        
        self.use_transparent = tk.BooleanVar(value=False)
        ttk.Checkbutton(trans_frame, text="Replace transparent with:", 
                        variable=self.use_transparent).grid(row=0, column=0)
        
        self.trans_color_var = tk.StringVar(value="black")
        trans_combo = ttk.Combobox(trans_frame, textvariable=self.trans_color_var,
                                    values=["black", "white", "magenta"],
                                    state="readonly", width=10)
        trans_combo.grid(row=0, column=1, padx=5)
        
        ttk.Label(trans_frame, text="(for PNG transparency)", 
                  font=('Segoe UI', 8), foreground='gray').grid(row=0, column=2)
        
        # --- Preview Frame ---
        preview_frame = ttk.LabelFrame(main_frame, text="Preview", padding="10")
        preview_frame.grid(row=2, column=0, columnspan=2, sticky="ew", pady=5)
        preview_frame.columnconfigure(0, weight=1)
        preview_frame.columnconfigure(1, weight=1)
        
        # Original preview
        orig_frame = ttk.Frame(preview_frame)
        orig_frame.grid(row=0, column=0, padx=10)
        
        ttk.Label(orig_frame, text="Original", font=('Segoe UI', 9, 'bold')).grid(row=0, column=0)
        self.preview_label = ttk.Label(orig_frame, text="No image loaded", anchor="center")
        self.preview_label.grid(row=1, column=0, pady=5)
        
        # RGB565 render test preview
        render_frame = ttk.Frame(preview_frame)
        render_frame.grid(row=0, column=1, padx=10)
        
        ttk.Label(render_frame, text="RGB565 Render Test", font=('Segoe UI', 9, 'bold')).grid(row=0, column=0)
        self.render_preview_label = ttk.Label(render_frame, text="Convert to preview", anchor="center")
        self.render_preview_label.grid(row=1, column=0, pady=5)
        
        self.info_label = ttk.Label(preview_frame, text="", font=('Segoe UI', 9))
        self.info_label.grid(row=1, column=0, columnspan=2)
        
        # --- Buttons ---
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=3, column=0, columnspan=2, pady=10)
        
        ttk.Button(button_frame, text="Convert", command=self.convert,
                   style='Accent.TButton').grid(row=0, column=0, padx=5)
        ttk.Button(button_frame, text="Save to File...", command=self.save_file).grid(
            row=0, column=1, padx=5)
        ttk.Button(button_frame, text="Copy to Clipboard", command=self.copy_to_clipboard).grid(
            row=0, column=2, padx=5)
        
        # --- Usage Info ---
        usage_frame = ttk.LabelFrame(main_frame, text="Cardputer Usage", padding="5")
        usage_frame.grid(row=4, column=0, columnspan=2, sticky="ew", pady=5)
        
        usage_text = """In your .ino file (RGB565_SWAPPED is pre-swapped, no setSwapBytes needed):
  #include "yourimage.h"
  
  sprite.pushImage(x, y, yourimage_width, yourimage_height, yourimage);"""
        
        ttk.Label(usage_frame, text=usage_text, font=('Consolas', 9), 
                  foreground='#444').grid(row=0, column=0, sticky="w")
        
        # --- Output Frame ---
        output_frame = ttk.LabelFrame(main_frame, text="Output", padding="10")
        output_frame.grid(row=5, column=0, columnspan=2, sticky="nsew", pady=5)
        output_frame.columnconfigure(0, weight=1)
        output_frame.rowconfigure(0, weight=1)
        
        self.output_text = scrolledtext.ScrolledText(output_frame, wrap=tk.NONE,
                                                      font=('Consolas', 9))
        self.output_text.grid(row=0, column=0, sticky="nsew")
        
        # Horizontal scrollbar
        h_scroll = ttk.Scrollbar(output_frame, orient="horizontal",
                                  command=self.output_text.xview)
        h_scroll.grid(row=1, column=0, sticky="ew")
        self.output_text.configure(xscrollcommand=h_scroll.set)
        
        # Store the last conversion result
        self.last_result = None
    
    def browse_file(self):
        filepath = filedialog.askopenfilename(
            title="Select Image",
            filetypes=[
                ("Image files", "*.png *.jpg *.jpeg *.bmp *.gif"),
                ("PNG files", "*.png"),
                ("All files", "*.*")
            ]
        )
        if filepath:
            self.file_entry.delete(0, tk.END)
            self.file_entry.insert(0, filepath)
            self.image_path = filepath
            self.load_preview()
            
            # Auto-set variable name from filename
            var_name = Path(filepath).stem
            var_name = ''.join(c if c.isalnum() or c == '_' else '_' for c in var_name)
            if var_name[0].isdigit():
                var_name = '_' + var_name
            self.var_name_entry.delete(0, tk.END)
            self.var_name_entry.insert(0, var_name)
    
    def load_preview(self):
        try:
            img = Image.open(self.image_path)
            width, height = img.size
            
            # Resize for preview (max 150x150)
            max_size = 150
            ratio = min(max_size / width, max_size / height, 1.0)
            new_size = (int(width * ratio), int(height * ratio))
            preview_img = img.resize(new_size, Image.Resampling.LANCZOS)
            
            self.preview_image = ImageTk.PhotoImage(preview_img)
            self.preview_label.configure(image=self.preview_image, text="")
            
            mode_info = f" ({img.mode})" if img.mode == "RGBA" else ""
            self.info_label.configure(text=f"Size: {width} x {height} pixels{mode_info}")
            
        except Exception as e:
            self.preview_label.configure(image="", text=f"Error: {e}")
            self.info_label.configure(text="")
    
    def create_rgb565_preview(self, filepath, transparent_color=None):
        """Create a preview showing how the image will look after RGB565 conversion."""
        try:
            img = Image.open(filepath)
            width, height = img.size
            
            # Always convert to RGBA to handle all transparency types
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            pixels = list(img.getdata())
            
            # Convert through RGB565 and back to show actual rendered colors
            new_pixels = []
            for pixel in pixels:
                r, g, b, a = pixel
                # Replace transparent pixels
                if a < 128 and transparent_color:
                    r, g, b = transparent_color
                
                # Convert to RGB565 and back
                color565 = rgb_to_rgb565(r, g, b)
                new_rgb = rgb565_to_rgb(color565)
                new_pixels.append(new_rgb)
            
            # Create preview image
            preview_img = Image.new('RGB', (width, height))
            preview_img.putdata(new_pixels)
            
            # Resize for preview (max 150x150)
            max_size = 150
            ratio = min(max_size / width, max_size / height, 1.0)
            new_size = (int(width * ratio), int(height * ratio))
            preview_img = preview_img.resize(new_size, Image.Resampling.NEAREST)  # NEAREST to show pixel accuracy
            
            self.render_preview_image = ImageTk.PhotoImage(preview_img)
            self.render_preview_label.configure(image=self.render_preview_image, text="")
            
        except Exception as e:
            self.render_preview_label.configure(image="", text=f"Error: {e}")
    
    def convert(self):
        filepath = self.file_entry.get()
        if not filepath:
            messagebox.showwarning("Warning", "Please select an image file first.")
            return
        
        if not Path(filepath).exists():
            messagebox.showerror("Error", f"File not found: {filepath}")
            return
        
        try:
            var_name = self.var_name_entry.get().strip()
            format_type = self.format_var.get()
            
            # Handle transparent color
            transparent_color = None
            if self.use_transparent.get():
                color_map = {
                    "black": (0, 0, 0),
                    "white": (255, 255, 255),
                    "magenta": (255, 0, 255)
                }
                transparent_color = color_map.get(self.trans_color_var.get())
            
            result, width, height, array_len = convert_png_to_cpp(
                filepath, format_type, var_name, transparent_color
            )
            
            self.output_text.delete(1.0, tk.END)
            self.output_text.insert(1.0, result)
            self.last_result = result
            
            # Create RGB565 render preview
            self.create_rgb565_preview(filepath, transparent_color)
            
            # Calculate file size
            if format_type in ('rgb565', 'rgb565_swapped', 'cardputer'):
                byte_size = array_len * 2
            else:
                byte_size = array_len
            
            messagebox.showinfo("Success", 
                f"Converted successfully!\n\n"
                f"Image: {width}x{height} pixels\n"
                f"Format: {format_type.upper()}\n"
                f"Array elements: {array_len}\n"
                f"Data size: {byte_size} bytes")
            
        except Exception as e:
            messagebox.showerror("Error", f"Conversion failed:\n{e}")
    
    def save_file(self):
        if not self.last_result:
            messagebox.showwarning("Warning", "Please convert an image first.")
            return
        
        # Suggest filename based on variable name
        var_name = self.var_name_entry.get().strip() or "image"
        
        filepath = filedialog.asksaveasfilename(
            title="Save Header File",
            defaultextension=".h",
            initialfile=f"{var_name}.h",
            filetypes=[
                ("C/C++ Header", "*.h"),
                ("All files", "*.*")
            ]
        )
        if filepath:
            try:
                with open(filepath, 'w') as f:
                    f.write(self.last_result)
                messagebox.showinfo("Success", f"Saved to:\n{filepath}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to save file:\n{e}")
    
    def copy_to_clipboard(self):
        if not self.last_result:
            messagebox.showwarning("Warning", "Please convert an image first.")
            return
        
        self.root.clipboard_clear()
        self.root.clipboard_append(self.last_result)
        messagebox.showinfo("Success", "Copied to clipboard!")


def main():
    root = tk.Tk()
    
    # Try to set a modern theme
    try:
        root.tk.call("source", "sun-valley.tcl")
        root.tk.call("set_theme", "light")
    except:
        pass  # Use default theme if sun-valley not available
    
    app = PngToCppConverter(root)
    root.mainloop()


if __name__ == "__main__":
    main()
