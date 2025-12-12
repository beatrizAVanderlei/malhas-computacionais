import csv
import statistics

def read_performance_data(filename):
    vertices = []
    faces = []
    total_time = None
    with open(filename, 'r') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            tipo = row['Tipo']
            if tipo == 'v':
                index = int(row['Index'])
                time_faces = float(row['TempoFaces'])
                num_faces = int(row['NumFaces'])
                time_adjacent = float(row['TempoAdjacentes'])
                num_adjacent = int(row['NumAdjacentes'])
                vertices.append((index, time_faces, num_faces, time_adjacent, num_adjacent))
            elif tipo == 'f':
                index = int(row['Index'])
                time_vertices = float(row['TempoFaces'])
                num_vertices = int(row['NumFaces'])
                time_adjacent = float(row['TempoAdjacentes'])
                num_adjacent = int(row['NumAdjacentes'])
                faces.append((index, time_vertices, num_vertices, time_adjacent, num_adjacent))
            elif tipo == 'total':
                total_time = float(row['TempoFaces'])
    return vertices, faces, total_time

def compute_statistics(values):
    # Filtra valores negativos antes de calcular as estatísticas
    values = [v for v in values if v >= 0]

    if not values:
        return None, None, None, None

    mean_val = statistics.mean(values)
    min_val = min(values)
    max_val = max(values)
    stdev_val = statistics.stdev(values) if len(values) > 1 else 0
    return mean_val, min_val, max_val, stdev_val

def analyze_performance_data(input_filename, output_txt="performance_results.txt"):
    vertices, faces, total_time = read_performance_data(input_filename)

    # Extraia os tempos para vértices
    vertex_times_faces = [t for (_, t, _, _, _) in vertices]
    vertex_num_faces   = [n for (_, _, n, _, _) in vertices]
    vertex_times_adj   = [t for (_, _, _, t, _) in vertices]
    vertex_num_adj     = [n for (_, _, _, _, n) in vertices]

    # Extraia os tempos para faces
    face_times_vertices = [t for (_, t, _, _, _) in faces]
    face_num_vertices   = [n for (_, _, n, _, _) in faces]
    face_times_adj      = [t for (_, _, _, t, _) in faces]
    face_num_adj        = [n for (_, _, _, _, n) in faces]

    # Compute statistics para vértices
    v_tf_stats = compute_statistics(vertex_times_faces)
    v_nf_stats = compute_statistics(vertex_num_faces)
    v_ta_stats = compute_statistics(vertex_times_adj)
    v_na_stats = compute_statistics(vertex_num_adj)

    # Compute statistics para faces
    f_tv_stats = compute_statistics(face_times_vertices)
    f_nv_stats = compute_statistics(face_num_vertices)
    f_ta_stats = compute_statistics(face_times_adj)
    f_na_stats = compute_statistics(face_num_adj)

    # Verificar se o tempo total de execução é negativo
    if total_time < 0:
        total_time = 0  # Corrige caso o tempo total seja negativo

    output = []
    output.append(f"Quantidade de vértices: {len(vertices)}")
    output.append(f"Quantidade de faces: {len(faces)}")
    output.append("=== Estatísticas para vértices ===")
    output.append(f"Tempo para acessar faces: média={v_tf_stats[0]:.6f}, min={v_tf_stats[1]:.6f}, max={v_tf_stats[2]:.6f}, stdev={v_tf_stats[3]:.6f}")
    output.append(f"Número de faces: média={v_nf_stats[0]:.2f}, min={v_nf_stats[1]}, max={v_nf_stats[2]}, stdev={v_nf_stats[3]:.2f}")
    output.append(f"Tempo para acessar vizinhos: média={v_ta_stats[0]:.6f}, min={v_ta_stats[1]:.6f}, max={v_ta_stats[2]:.6f}, stdev={v_ta_stats[3]:.6f}")
    output.append(f"Número de vizinhos: média={v_na_stats[0]:.2f}, min={v_na_stats[1]}, max={v_na_stats[2]}, stdev={v_na_stats[3]:.2f}")
    output.append("=== Estatísticas para faces ===")
    output.append(f"Tempo para acessar vértices: média={f_tv_stats[0]:.6f}, min={f_tv_stats[1]:.6f}, max={f_tv_stats[2]:.6f}, stdev={f_tv_stats[3]:.6f}")
    output.append(f"Número de vértices: média={f_nv_stats[0]:.2f}, min={f_nv_stats[1]}, max={f_nv_stats[2]}, stdev={f_nv_stats[3]:.2f}")
    output.append(f"Tempo para acessar vizinhos: média={f_ta_stats[0]:.6f}, min={f_ta_stats[1]:.6f}, max={f_ta_stats[2]:.6f}, stdev={f_ta_stats[3]:.6f}")
    output.append(f"Número de vizinhos: média={f_na_stats[0]:.2f}, min={f_na_stats[1]}, max={f_na_stats[2]}, stdev={f_na_stats[3]:.2f}")
    output.append(f"Tempo total de execução (do C++): {total_time:.6f} segundos")

    with open(output_txt, "w") as f_out:
        f_out.write("\n".join(output))

if __name__ == "__main__":
    input_file = "C:/Users/bia/CLionProjects/teste/src/mate-face/performance-heart-mf.csv"
    analyze_performance_data(input_file, "C:/Users/bia/CLionProjects/teste/src/mate-face/performance-results-heart-mf.txt")
