#!/usr/bin/env python3
import sys
import os

try:
    from PIL import Image
except ImportError:
    print("Erro: A biblioteca 'Pillow' nao esta instalada.")
    print("Instale-a executando: pip install Pillow")
    sys.exit(1)

def block_to_bytes(block):
    """Converte um bloco de 5x8 (matriz 8x5 de 0/1) em uma tupla de 8 bytes (inteiros de 5-bits)."""
    bytes_list = []
    for r in range(8):
        val = 0
        for c in range(5):
            val = (val << 1) | block[r][c]
        bytes_list.append(val)
    return tuple(bytes_list)

def bytes_to_block(bytes_tuple):
    """Converte uma tupla de 8 bytes de volta em uma matriz 8x5 de 0/1."""
    block = []
    for val in bytes_tuple:
        row = []
        for shift in reversed(range(5)):
            row.append((val >> shift) & 1)
        block.append(row)
    return block

def get_hamming_distance(block1, block2):
    """Calcula a distancia Hamming (diferenca de pixels) entre dois blocos 8x5."""
    diff = 0
    for r in range(8):
        for c in range(5):
            if block1[r][c] != block2[r][c]:
                diff += 1
    return diff

def encode_image(image_path, threshold=128, invert=False):
    if not os.path.exists(image_path):
        print(f"Erro: Arquivo nao encontrado em '{image_path}'")
        sys.exit(1)

    # Abre a imagem e converte para escala de cinza
    img = Image.open(image_path).convert('L')
    
    # Redimensiona para resolucao exata do grid do LCD (100x32 pixels = 20x4 caracteres de 5x8 pixels)
    img = img.resize((100, 32), Image.Resampling.LANCZOS)
    
    pixels = img.load()
    width, height = img.size
    
    # Cria a matriz binarizada da imagem original (1 = aceso, 0 = apagado)
    original_grid = []
    for y in range(height):
        row = []
        for x in range(width):
            gray = pixels[x, y]
            is_active = gray > threshold if invert else gray < threshold
            row.append(1 if is_active else 0)
        original_grid.append(row)
        
    # Segmenta em 80 celulas de 5x8
    cells = []
    for row_idx in range(4):
        for col_idx in range(20):
            cell = []
            for y in range(8):
                cell_row = []
                for x in range(5):
                    cell_row.append(original_grid[row_idx * 8 + y][col_idx * 5 + x])
                cell.append(cell_row)
            cells.append(cell)

    # Conta frequencia dos padroes nao-triviais (ignorando totalmente vazio e totalmente cheio)
    pattern_counts = {}
    empty_pattern = (0, 0, 0, 0, 0, 0, 0, 0)
    solid_pattern = (31, 31, 31, 31, 31, 31, 31, 31)

    for cell in cells:
        p_bytes = block_to_bytes(cell)
        if p_bytes == empty_pattern or p_bytes == solid_pattern:
            continue
        pattern_counts[p_bytes] = pattern_counts.get(p_bytes, 0) + 1

    # Seleciona os 8 padroes mais frequentes para serem os caracteres customizados
    sorted_patterns = sorted(pattern_counts.items(), key=lambda x: x[1], reverse=True)
    custom_patterns = [p[0] for p in sorted_patterns[:8]]
    num_custom = len(custom_patterns)

    # Define os templates de busca
    search_templates = {}
    search_templates['.'] = bytes_to_block(empty_pattern)
    search_templates['#'] = bytes_to_block(solid_pattern)
    for idx, p in enumerate(custom_patterns):
        search_templates[str(idx)] = bytes_to_block(p)

    # Mapeia cada celula para o melhor template correspondente
    encoded_chars = []
    reconstructed_grid = [[0 for _ in range(100)] for _ in range(32)]

    for cell_idx, cell in enumerate(cells):
        best_char = '.'
        min_diff = 40
        
        for char, template in search_templates.items():
            diff = get_hamming_distance(cell, template)
            if diff < min_diff:
                min_diff = diff
                best_char = char
                
        encoded_chars.append(best_char)
        
        # Reconstrói a imagem para a visualização
        best_template = search_templates[best_char]
        row_idx = cell_idx // 20
        col_idx = cell_idx % 20
        for y in range(8):
            for x in range(5):
                reconstructed_grid[row_idx * 8 + y][col_idx * 5 + x] = best_template[y][x]

    # Imprime visualizacao previa original
    print("\n[1] IMAGEM ORIGINAL BINARIZADA (100x32):")
    print("+" + "-" * 100 + "+")
    for y in range(32):
        row_str = "".join("█" if original_grid[y][x] else " " for x in range(100))
        print("|" + row_str + "|")
    print("+" + "-" * 100 + "+")

    # Imprime visualizacao previa reconstruida (com customizacao dinamica da CGRAM)
    print("\n[2] IMAGEM RECONSTRUIDA DINAMICAMENTE NO LCD (100x32):")
    print("+" + "-" * 100 + "+")
    for y in range(32):
        row_str = "".join("█" if reconstructed_grid[y][x] else " " for x in range(100))
        print("|" + row_str + "|")
    print("+" + "-" * 100 + "+")
    
    print(f"Caracteres personalizados gerados para esta imagem: {num_custom} (limite: 8)")

    # Montagem do pacote binario de dados
    # Header: 4 bytes [0x1B, 'I', 'M', 'G']
    payload = bytearray([0x1B, ord('I'), ord('M'), ord('G')])
    # Quantidade de caracteres customizados: 1 byte
    payload.append(num_custom)
    
    # 8 bytes de bitmap por caractere customizado
    for p in custom_patterns:
        for b in p:
            payload.append(b)
            
    # 80 bytes de layout da tela
    for char in encoded_chars:
        payload.append(ord(char))

    # Converte para string hexadecimal
    hex_str = payload.hex()
    
    print("\nString de comando para colar no Serial Monitor (inclui definicao de caracteres):")
    print("-" * 80)
    print(f"IMG:{hex_str}")
    print("-" * 80)
    print(f"Comprimento da transmissao: {len(payload)} bytes (dividida em {int((len(payload) + 23) / 24)} fragmentos)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python image_encoder.py <caminho_da_imagem> [threshold] [invert]")
        print("Exemplo: python image_encoder.py coracao.png 128")
        sys.exit(1)
        
    img_path = sys.argv[1]
    thresh = int(sys.argv[2]) if len(sys.argv) > 2 else 128
    inv = sys.argv[3].lower() in ['true', '1', 'yes', 'invert'] if len(sys.argv) > 3 else False
    
    encode_image(img_path, threshold=thresh, invert=inv)
