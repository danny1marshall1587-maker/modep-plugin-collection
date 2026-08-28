from PIL import Image, ImageDraw, ImageFont
import os

def create_pedal_graphic(plugin_type, width, height):
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    scale = width / 340.0
    
    # Margin
    pad_x = int(25 * scale)
    pad_y = int(20 * scale)
    body_w = width - (pad_x * 2)
    body_h = height - (pad_y * 2)
    corner = int(14 * scale)
    
    if plugin_type == 'harmonic-tremolo':
        bg_color = (46, 29, 19, 255)
        border_color = (196, 154, 69, 255)
        led_color = (255, 170, 0, 255)
        title_text = "HARMONIC TREM"
        brand_text = "CYBER AUDIO"
        sub_text = "VINTAGE TRI-VERB"
        knob_color = (245, 235, 200, 255)
        accent_color = (212, 175, 55, 255)
    elif plugin_type == 'cyber-denoiser':
        bg_color = (26, 10, 38, 255)
        border_color = (212, 0, 255, 255)
        led_color = (212, 0, 255, 255)
        title_text = "CYBER-DENOISER"
        brand_text = "CYBER AUDIO"
        sub_text = "PRO SUPPRESSOR"
        knob_color = (187, 134, 252, 255)
        accent_color = (0, 229, 255, 255)
    elif plugin_type == 'dimension-c':
        bg_color = (58, 29, 82, 255)
        border_color = (180, 100, 230, 255)
        led_color = (0, 229, 255, 255)
        title_text = "DIMENSION-C"
        brand_text = "CYBER AUDIO"
        sub_text = "SPATIAL CHORUS"
        knob_color = (220, 180, 245, 255)
        accent_color = (230, 184, 255, 255)
    elif plugin_type == 'guitar-midi':
        bg_color = (10, 22, 38, 255)
        border_color = (0, 229, 255, 255)
        led_color = (0, 229, 255, 255)
        title_text = "GUITAR-TO-MIDI"
        brand_text = "CYBER AUDIO"
        sub_text = "SYNTH CONTROLLER"
        knob_color = (120, 220, 245, 255)
        accent_color = (0, 229, 255, 255)
    elif plugin_type == 'bluesbreaker.lv2':
        bg_color = (18, 18, 20, 255)
        border_color = (100, 140, 190, 255)
        led_color = (255, 20, 20, 255)
        title_text = "BLUESBREAKER"
        brand_text = "BRUMMER"
        sub_text = "VINTAGE OVERDRIVE"
        knob_color = (230, 200, 110, 255)
        accent_color = (120, 175, 230, 255)
    elif plugin_type == 'nam-loader.lv2':
        bg_color = (24, 18, 12, 255)
        border_color = (220, 150, 40, 255)
        led_color = (255, 170, 0, 255)
        title_text = "NAM LOADER"
        brand_text = "CYBER AUDIO"
        sub_text = "TONE 3000 ENGINE"
        knob_color = (240, 200, 120, 255)
        accent_color = (255, 180, 50, 255)
    elif plugin_type == 'cyber-hum-killer.lv2':
        bg_color = (20, 12, 4, 255)
        border_color = (255, 170, 0, 255)
        led_color = (255, 170, 0, 255)
        title_text = "CYBER HUM KILLER"
        brand_text = "CYBER AUDIO"
        sub_text = "DC & MAINS SUPPRESSOR"
        knob_color = (255, 190, 80, 255)
        accent_color = (255, 170, 0, 255)
    elif plugin_type == 'smart-fizz-killer.lv2':
        bg_color = (32, 18, 8, 255)
        border_color = (205, 127, 50, 255)
        led_color = (255, 140, 0, 255)
        title_text = "SMART FIZZ KILLER"
        brand_text = "CYBER AUDIO"
        sub_text = "DIGITAL SMOOTHER"
        knob_color = (220, 160, 100, 255)
        accent_color = (230, 150, 60, 255)
    else: # galaxy-strobe-tune
        bg_color = (5, 11, 20, 255)
        border_color = (0, 229, 255, 255)
        led_color = (0, 229, 255, 255)
        title_text = "GALAXY STROBE"
        brand_text = "CYBER AUDIO"
        sub_text = "PRECISION TUNER"
        knob_color = (121, 166, 210, 255)
        accent_color = (255, 0, 127, 255)
        
    # Draw Chassis with rounded corners
    draw.rounded_rectangle([pad_x, pad_y, pad_x + body_w, pad_y + body_h], radius=corner, fill=bg_color, outline=border_color, width=int(3 * scale))
    
    # Header bar
    hdr_h = int(50 * scale)
    draw.rectangle([pad_x + 3, pad_y + 3, pad_x + body_w - 3, pad_y + hdr_h], fill=(0, 0, 0, 80))
    
    # Text labels
    draw.text((pad_x + body_w // 2, pad_y + int(12 * scale)), brand_text, fill=accent_color, anchor="mm")
    draw.text((pad_x + body_w // 2, pad_y + int(28 * scale)), title_text, fill=(255, 255, 255, 255), anchor="mm")
    draw.text((pad_x + body_w // 2, pad_y + int(42 * scale)), sub_text, fill=accent_color, anchor="mm")
    
    # Specific UI elements
    if plugin_type == 'harmonic-tremolo':
        for row in range(2):
            for col in range(3):
                kx = pad_x + int((45 + col * 75) * scale)
                ky = pad_y + int((85 + row * 65) * scale)
                kr = int(18 * scale)
                draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=knob_color, outline=(80, 50, 30, 255), width=int(2 * scale))
                draw.line([kx, ky, kx, ky - kr + int(2 * scale)], fill=(40, 20, 10, 255), width=int(3 * scale))
    elif plugin_type == 'cyber-denoiser':
        btn_w = int(180 * scale)
        btn_h = int(35 * scale)
        bx = pad_x + (body_w - btn_w) // 2
        by = pad_y + int(65 * scale)
        draw.rounded_rectangle([bx, by, bx + btn_w, by + btn_h], radius=int(16 * scale), fill=(138, 43, 226, 255), outline=border_color, width=int(2 * scale))
        draw.text((pad_x + body_w // 2, by + btn_h // 2), "LEARN NOISE", fill=(255, 255, 255, 255), anchor="mm")
        
        kx = pad_x + int(80 * scale)
        ky = pad_y + int(150 * scale)
        kr = int(24 * scale)
        draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=knob_color, outline=(100, 0, 150, 255), width=int(2 * scale))
        draw.line([kx, ky, kx, ky - kr + int(2 * scale)], fill=(40, 0, 60, 255), width=int(3 * scale))
    elif plugin_type == 'cyber-hum-killer.lv2':
        btn_w = int(140 * scale)
        btn_h = int(30 * scale)
        bx = pad_x + int(20 * scale)
        by = pad_y + int(60 * scale)
        draw.rounded_rectangle([bx, by, bx + btn_w, by + btn_h], radius=int(8 * scale), fill=(255, 170, 0, 255), outline=border_color, width=int(2 * scale))
        draw.text((bx + btn_w // 2, by + btn_h // 2), "LEARN HUM", fill=(0, 0, 0, 255), anchor="mm")
        
        # 8 mini slider representations
        sw = int(240 * scale)
        sh = int(85 * scale)
        sx = pad_x + (body_w - sw) // 2
        sy = pad_y + int(105 * scale)
        draw.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=int(6 * scale), fill=(8, 5, 2, 255), outline=(120, 70, 10, 255), width=int(1.5 * scale))
        for i in range(8):
            lx = sx + int((15 + i * 28) * scale)
            draw.line([lx, sy + int(10 * scale), lx, sy + sh - int(10 * scale)], fill=(80, 50, 20, 255), width=int(2 * scale))
            draw.rectangle([lx - int(4 * scale), sy + int(40 * scale), lx + int(4 * scale), sy + int(48 * scale)], fill=knob_color)
    elif plugin_type == 'dimension-c':
        for i in range(4):
            bx = pad_x + int((35 + i * 58) * scale)
            by = pad_y + int(70 * scale)
            bw = int(45 * scale)
            bh = int(65 * scale)
            b_col = (100, 40, 140, 255) if i == 0 else (40, 18, 55, 255)
            draw.rounded_rectangle([bx, by, bx + bw, by + bh], radius=int(6 * scale), fill=b_col, outline=border_color, width=int(2 * scale))
            led_c = (0, 229, 255, 255) if i == 0 else (60, 20, 80, 255)
            draw.ellipse([bx + bw//2 - 4, by + 8, bx + bw//2 + 4, by + 16], fill=led_c)
            draw.text((bx + bw // 2, by + int(38 * scale)), ["I", "II", "III", "IV"][i], fill=(255, 255, 255, 255), anchor="mm")
            
        for i, lbl in enumerate(["WIDTH", "MIX"]):
            kx = pad_x + int((80 + i * 110) * scale)
            ky = pad_y + int(165 * scale)
            kr = int(18 * scale)
            draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=knob_color, outline=(70, 30, 90, 255), width=int(2 * scale))
    elif plugin_type == 'guitar-midi':
        sw = int(220 * scale)
        sh = int(95 * scale)
        sx = pad_x + (body_w - sw) // 2
        sy = pad_y + int(60 * scale)
        draw.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=int(8 * scale), fill=(2, 8, 18, 255), outline=(0, 180, 210, 255), width=int(2 * scale))
        draw.text((sx + sw // 2, sy + int(28 * scale)), "E 2", fill=(0, 229, 255, 255), anchor="mm")
        
        kw = int(150 * scale)
        kh = int(22 * scale)
        kx = sx + (sw - kw) // 2
        ky = sy + int(55 * scale)
        draw.rectangle([kx, ky, kx + kw, ky + kh], fill=(220, 220, 220, 255), outline=(50, 50, 50, 255))
        
        for i, lbl in enumerate(["GAIN", "SENSE", "DYN", "BEND"]):
            px = pad_x + int((40 + i * 55) * scale)
            py = pad_y + int(175 * scale)
            pr = int(14 * scale)
            draw.ellipse([px - pr, py - pr, px + pr, py + pr], fill=knob_color, outline=(0, 100, 120, 255), width=int(1.5 * scale))
    elif plugin_type == 'bluesbreaker.lv2':
        for i, lbl in enumerate(["GAIN", "TONE", "VOL"]):
            px = pad_x + int((45 + i * 75) * scale)
            py = pad_y + int(115 * scale)
            pr = int(22 * scale)
            draw.ellipse([px - pr, py - pr, px + pr, py + pr], fill=knob_color, outline=(80, 70, 40, 255), width=int(2 * scale))
            draw.line([px, py, px, py - pr + int(3 * scale)], fill=(40, 30, 10, 255), width=int(3 * scale))
    elif plugin_type == 'nam-loader.lv2':
        # OLED Screen
        sw = int(220 * scale)
        sh = int(75 * scale)
        sx = pad_x + (body_w - sw) // 2
        sy = pad_y + int(60 * scale)
        draw.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=int(8 * scale), fill=(8, 6, 3, 255), outline=(180, 110, 20, 255), width=int(2 * scale))
        draw.text((sx + sw // 2, sy + int(24 * scale)), "Marshall JCM800", fill=(255, 255, 255, 255), anchor="mm")
        draw.text((sx + sw // 2, sy + int(48 * scale)), "TONE 3000 CAPTURE", fill=accent_color, anchor="mm")
        
        # 2 Rows of 4 Knobs
        for row in range(2):
            for col in range(4):
                kx = pad_x + int((35 + col * 55) * scale)
                ky = pad_y + int((155 + row * 45) * scale)
                kr = int(12 * scale)
                draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=knob_color, outline=(80, 50, 10, 255), width=int(1.5 * scale))
    else: # galaxy-strobe-tune
        sw = int(200 * scale)
        sh = int(100 * scale)
        sx = pad_x + (body_w - sw) // 2
        sy = pad_y + int(60 * scale)
        draw.rounded_rectangle([sx, sy, sx + sw, sy + sh], radius=int(8 * scale), fill=(2, 7, 16, 255), outline=(0, 95, 115, 255), width=int(2 * scale))
        
        cx = sx + sw // 2
        cy = sy + sh // 2
        cr = int(38 * scale)
        draw.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], outline=(0, 229, 255, 180), width=int(2 * scale))
        draw.text((cx, cy), "E", fill=(255, 255, 255, 255), anchor="mm")
        
        for i, lbl in enumerate(["GAIN", "STAB", "MUTE", "REF A"]):
            kx = pad_x + int((40 + i * 55) * scale)
            ky = pad_y + int(180 * scale)
            kr = int(12 * scale)
            draw.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=knob_color, outline=(0, 95, 115, 255), width=int(1.5 * scale))
            
    # LED
    lx = pad_x + body_w // 2
    ly = pad_y + body_h - int(55 * scale)
    lr = int(8 * scale)
    draw.ellipse([lx - lr, ly - lr, lx + lr, ly + lr], fill=led_color, outline=(255, 255, 255, 200), width=int(1.5 * scale))
    
    # 3D Stomp Footswitch
    fx = pad_x + body_w // 2
    fy = pad_y + body_h - int(25 * scale)
    fr_outer = int(18 * scale)
    fr_inner = int(12 * scale)
    draw.ellipse([fx - fr_outer, fy - fr_outer, fx + fr_outer, fy + fr_outer], fill=(160, 160, 165, 255), outline=(80, 80, 85, 255), width=int(2 * scale))
    draw.ellipse([fx - fr_inner, fy - fr_inner, fx + fr_inner, fy + fr_inner], fill=(220, 220, 225, 255), outline=(120, 120, 125, 255), width=int(1.5 * scale))
    
    return img

plugins = ['harmonic-tremolo', 'cyber-denoiser', 'galaxy-strobe-tune', 'dimension-c', 'guitar-midi', 'bluesbreaker.lv2', 'nam-loader.lv2', 'cyber-hum-killer.lv2', 'smart-fizz-killer.lv2']
for p in plugins:
    out_dir = os.path.join('plugins', p, 'modgui')
    os.makedirs(out_dir, exist_ok=True)
    
    thumb = create_pedal_graphic(p, 340, 260)
    thumb.save(os.path.join(out_dir, 'thumbnail.png'), 'PNG')
    
    ss = create_pedal_graphic(p, 680, 520)
    ss.save(os.path.join(out_dir, 'screenshot.png'), 'PNG')
    
    print(f"Generated GUI images for {p}")
